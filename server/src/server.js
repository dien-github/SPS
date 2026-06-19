const fs = require('fs/promises');
const path = require('path');

const cors = require('cors');
const express = require('express');
require('dotenv').config();

const {
  dbHealth,
  deleteLecturer,
  ensureSchema,
  getRoomConfig,
  listRooms,
  listLecturers,
  recordEvent,
  updateDeviceState,
  upsertLecturer,
} = require('./db');
const { createMqttWorker } = require('./mqttWorker');

const app = express();
const PORT = Number(process.env.PORT || 3000);
const OTA_DIR = process.env.OTA_DIR || path.join(__dirname, '..', 'ota');
const DASHBOARD_DIR = process.env.DASHBOARD_DIR
  || path.join(__dirname, '..', 'webdashboard', 'public');

app.use(cors());
app.use(express.json({ limit: '1mb' }));
app.use('/ota', express.static(OTA_DIR));

const mqttWorker = createMqttWorker({ onEvent: recordEvent });

function asyncHandler(handler) {
  return (req, res, next) => {
    Promise.resolve(handler(req, res, next)).catch(next);
  };
}

function validateLecturer(body) {
  const id = String(body.id || '').trim();
  const name = String(body.name || '').trim();
  const rfid = String(body.rfid || body.code || body.card_id || '').trim();
  const authorized = body.authorized ?? body.enabled ?? true;

  if (!id || !name || !rfid) {
    const error = new Error('id, name, and rfid are required');
    error.statusCode = 400;
    throw error;
  }

  return {
    id,
    name,
    rfid,
    authorized: Boolean(authorized),
  };
}

function normalizeDeviceState(value) {
  const raw = String(value ?? '').trim().toLowerCase();
  if (['on', 'true', '1', 'enabled'].includes(raw)) {
    return 'on';
  }
  if (['off', 'false', '0', 'disabled'].includes(raw)) {
    return 'off';
  }

  const error = new Error('state must be on or off');
  error.statusCode = 400;
  throw error;
}

app.get('/health', asyncHandler(async (req, res) => {
  let database = 'error';
  try {
    database = await dbHealth();
  } catch (err) {
    database = err.message;
  }

  res.json({
    status: database === 'ok' ? 'ok' : 'degraded',
    timestamp: new Date().toISOString(),
    database,
    mqtt: mqttWorker.getStatus(),
  });
}));

app.get('/api/lecturers', asyncHandler(async (req, res) => {
  res.json({ lecturers: await listLecturers() });
}));

app.post('/api/lecturers', asyncHandler(async (req, res) => {
  const lecturer = await upsertLecturer(validateLecturer(req.body));
  res.status(201).json({ lecturer });
}));

app.put('/api/lecturers/:lecturerId', asyncHandler(async (req, res) => {
  const lecturer = await upsertLecturer(validateLecturer({
    ...req.body,
    id: req.params.lecturerId,
  }));
  res.json({ lecturer });
}));

app.delete('/api/lecturers/:lecturerId', asyncHandler(async (req, res) => {
  const deleted = await deleteLecturer(req.params.lecturerId);
  if (!deleted) {
    res.status(404).json({ error: 'lecturer not found' });
    return;
  }
  res.json({ deleted: true });
}));

app.get('/api/rooms', asyncHandler(async (req, res) => {
  res.json({ rooms: await listRooms() });
}));

app.get('/api/rooms/:roomId/config', asyncHandler(async (req, res) => {
  const config = await getRoomConfig(req.params.roomId);
  if (!config.room) {
    res.status(404).json({ error: 'room not found' });
    return;
  }
  res.json(config);
}));

app.get('/api/rooms/:roomId/devices', asyncHandler(async (req, res) => {
  const config = await getRoomConfig(req.params.roomId);
  if (!config.room) {
    res.status(404).json({ error: 'room not found' });
    return;
  }
  res.json({ devices: config.devices });
}));

app.post('/api/rooms/:roomId/commands', asyncHandler(async (req, res) => {
  const commandType = String(req.body.command_type || req.body.type || '').trim();
  if (!commandType) {
    res.status(400).json({ error: 'command_type is required' });
    return;
  }

  const payload = {
    ...req.body,
    room_id: req.params.roomId,
    timestamp: new Date().toISOString(),
  };
  const published = await mqttWorker.publishCommand(req.params.roomId, commandType, payload);
  res.status(published ? 202 : 503).json({ published });
}));

app.post('/api/rooms/:roomId/devices/:deviceId/control', asyncHandler(async (req, res) => {
  const state = normalizeDeviceState(req.body.state ?? req.body.status);
  const config = await getRoomConfig(req.params.roomId);
  if (!config.room) {
    res.status(404).json({ error: 'room not found' });
    return;
  }

  const device = config.devices.find((item) => item.id === req.params.deviceId);
  if (!device) {
    res.status(404).json({ error: 'device not found' });
    return;
  }

  const commandType = device.type === 'projector' || device.type === 'ac'
    ? device.type
    : 'relay';
  const payload = {
    command_type: commandType,
    action: state,
    device_id: device.id,
    device_type: device.type,
    channel: device.metadata?.channel,
    room_id: req.params.roomId,
    timestamp: new Date().toISOString(),
  };

  const published = await mqttWorker.publishCommand(req.params.roomId, commandType, payload);
  const updatedDevice = await updateDeviceState(req.params.roomId, req.params.deviceId, state);
  res.status(202).json({ published, device: updatedDevice });
}));

app.post('/api/rooms/:roomId/sync', asyncHandler(async (req, res) => {
  const lecturers = await listLecturers();
  const payload = {
    command: 'sync_lecturers',
    room_id: req.params.roomId,
    lecturers,
    total_count: lecturers.length,
    timestamp: new Date().toISOString(),
  };
  const published = await mqttWorker.publishCommand(req.params.roomId, 'sync', payload);
  res.status(published ? 202 : 503).json({ published, total_count: lecturers.length });
}));

app.get('/api/ota', asyncHandler(async (req, res) => {
  await fs.mkdir(OTA_DIR, { recursive: true });
  const files = await fs.readdir(OTA_DIR);
  res.json({
    ota_dir: OTA_DIR,
    files: files.map((file) => ({
      file,
      url: `/ota/${encodeURIComponent(file)}`,
    })),
  });
}));

app.post('/api/rooms/:roomId/ota', asyncHandler(async (req, res) => {
  const version = String(req.body.version || '').trim();
  const url = String(req.body.url || '').trim();
  const checksum = String(req.body.checksum || '').trim();
  if (!version || !url) {
    res.status(400).json({ error: 'version and url are required' });
    return;
  }

  const published = await mqttWorker.publishCommand(req.params.roomId, 'ota', {
    version,
    url,
    checksum,
    timestamp: new Date().toISOString(),
  });
  res.status(published ? 202 : 503).json({ published });
}));

app.use('/api', (req, res) => {
  res.status(404).json({ error: 'api route not found' });
});

app.use(express.static(DASHBOARD_DIR));

app.get('*', (req, res) => {
  res.sendFile(path.join(DASHBOARD_DIR, 'index.html'));
});

app.use((err, req, res, next) => {
  const status = err.statusCode || 500;
  if (status >= 500) {
    console.error(err);
  }
  res.status(status).json({ error: err.message || 'internal server error' });
});

async function start() {
  await fs.mkdir(OTA_DIR, { recursive: true });

  try {
    await ensureSchema();
    console.log('Database schema ready');
  } catch (err) {
    console.error('Database schema init failed:', err.message);
  }

  mqttWorker.start();

  app.listen(PORT, () => {
    console.log(`SPS API and dashboard running on port ${PORT}`);
  });
}

start().catch((err) => {
  console.error('SPS API failed to start:', err);
  process.exit(1);
});
