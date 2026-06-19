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
  listEvents,
  listOverview,
  listRooms,
  listLecturers,
  recordEvent,
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

function commandTypeForDevice(device) {
  const protocol = String(device.protocol || device.metadata?.protocol || '').toLowerCase();
  if (protocol === 'relay') return 'relay';
  if (protocol === 'rs232') return 'projector';
  if (protocol === 'ir') return 'ac';
  if (protocol === 'wol') return 'wol';
  return device.type;
}

async function recordAuditEvent(roomId, type, payload) {
  await recordEvent({
    roomId,
    category: 'audit',
    type,
    topic: `admin/${type}`,
    payload: {
      ...payload,
      timestamp: new Date().toISOString(),
    },
  });
}

function httpError(message, statusCode = 400) {
  const error = new Error(message);
  error.statusCode = statusCode;
  return error;
}

function compactTimestamp(value) {
  return value.replace(/[-:.TZ]/g, '').slice(0, 14);
}

function parseMultipartBoundary(contentType) {
  const match = String(contentType || '').match(/boundary=(?:"([^"]+)"|([^;]+))/i);
  return match ? (match[1] || match[2]) : null;
}

function parseHeaderParams(value) {
  const params = {};
  String(value || '').split(';').slice(1).forEach((part) => {
    const [key, ...rest] = part.trim().split('=');
    if (!key || !rest.length) return;
    params[key.toLowerCase()] = rest.join('=').trim().replace(/^"|"$/g, '');
  });
  return params;
}

async function readRequestBuffer(req, maxBytes = 100 * 1024 * 1024) {
  const chunks = [];
  let total = 0;

  for await (const chunk of req) {
    total += chunk.length;
    if (total > maxBytes) {
      throw httpError('upload is too large', 413);
    }
    chunks.push(chunk);
  }

  return Buffer.concat(chunks);
}

function parseMultipart(buffer, boundary) {
  const delimiter = Buffer.from(`--${boundary}`);
  const newline = Buffer.from('\r\n');
  const headerEnd = Buffer.from('\r\n\r\n');
  const parts = [];
  let cursor = buffer.indexOf(delimiter);

  while (cursor !== -1) {
    let start = cursor + delimiter.length;
    if (buffer.slice(start, start + 2).equals(Buffer.from('--'))) break;
    if (buffer.slice(start, start + 2).equals(newline)) start += 2;

    const next = buffer.indexOf(delimiter, start);
    if (next === -1) break;

    let part = buffer.slice(start, next);
    if (part.slice(-2).equals(newline)) {
      part = part.slice(0, -2);
    }

    const headersEndAt = part.indexOf(headerEnd);
    if (headersEndAt !== -1) {
      const rawHeaders = part.slice(0, headersEndAt).toString('utf8');
      const body = part.slice(headersEndAt + headerEnd.length);
      const headers = {};
      rawHeaders.split('\r\n').forEach((line) => {
        const separator = line.indexOf(':');
        if (separator === -1) return;
        headers[line.slice(0, separator).trim().toLowerCase()] = line.slice(separator + 1).trim();
      });
      parts.push({ headers, body });
    }

    cursor = next;
  }

  return parts;
}

function getUploadedFirmware(parts) {
  for (const part of parts) {
    const disposition = part.headers['content-disposition'];
    if (!disposition || !/form-data/i.test(disposition)) continue;

    const params = parseHeaderParams(disposition);
    if (!params.filename) continue;

    return {
      field: params.name || 'file',
      originalName: params.filename,
      buffer: part.body,
    };
  }

  return null;
}

async function uniqueOtaFileName(fileName, uploadedAt) {
  const extension = path.extname(fileName);
  const base = path.basename(fileName, extension);
  let candidate = fileName;

  for (let index = 0; index < 100; index += 1) {
    const target = path.resolve(OTA_DIR, candidate);
    try {
      await fs.access(target);
      const suffix = index === 0 ? compactTimestamp(uploadedAt) : `${compactTimestamp(uploadedAt)}-${index}`;
      candidate = `${base}-${suffix}${extension}`;
    } catch {
      return { fileName: candidate, target };
    }
  }

  throw httpError('could not allocate unique firmware file name', 409);
}

async function parseFirmwareUpload(req) {
  const boundary = parseMultipartBoundary(req.headers['content-type']);
  if (!boundary) {
    throw httpError('multipart/form-data boundary is required');
  }

  const parts = parseMultipart(await readRequestBuffer(req), boundary);
  const upload = getUploadedFirmware(parts);
  if (!upload) {
    throw httpError('firmware .bin file is required');
  }

  const originalName = String(upload.originalName || '').trim();
  if (!originalName || /[\\/]/.test(originalName)) {
    throw httpError('invalid firmware file name');
  }

  const fileName = path.basename(originalName);
  if (path.extname(fileName).toLowerCase() !== '.bin') {
    throw httpError('only .bin firmware files are allowed');
  }

  if (!upload.buffer.length) {
    throw httpError('firmware file is empty');
  }

  const uploadedAt = new Date().toISOString();
  const { fileName: storedName, target } = await uniqueOtaFileName(fileName, uploadedAt);
  const otaRoot = path.resolve(OTA_DIR);
  if (!target.startsWith(`${otaRoot}${path.sep}`)) {
    throw httpError('invalid firmware path');
  }

  return {
    fileName: storedName,
    target,
    buffer: upload.buffer,
    uploadedAt,
  };
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

app.get('/api/overview', asyncHandler(async (req, res) => {
  res.json({ rooms: await listOverview() });
}));

app.get('/api/events', asyncHandler(async (req, res) => {
  res.json({
    events: await listEvents({
      roomId: req.query.roomId,
      category: req.query.category,
      type: req.query.type,
      limit: req.query.limit,
    }),
  });
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

  const commandType = commandTypeForDevice(device);
  const payload = {
    command_type: commandType,
    action: state,
    requested_state: state,
    device_id: device.id,
    device_type: device.type,
    protocol: device.protocol,
    channel: device.metadata?.channel,
    room_id: req.params.roomId,
    timestamp: new Date().toISOString(),
  };

  const published = await mqttWorker.publishCommand(req.params.roomId, commandType, payload);
  await recordAuditEvent(req.params.roomId, 'device_control', {
    action: 'requested',
    requested_state: state,
    mqtt_published: published,
    device_id: device.id,
    device_type: device.type,
    protocol: device.protocol,
  });
  res.status(202).json({
    published,
    physical_state_confirmed: false,
    device,
  });
}));

app.post('/api/rooms/sync', asyncHandler(async (req, res) => {
  const rooms = await listRooms();
  const lecturers = await listLecturers();
  let publishedCount = 0;

  for (const room of rooms) {
    const payload = {
      command: 'sync_lecturers',
      room_id: room.id,
      lecturers,
      total_count: lecturers.length,
      timestamp: new Date().toISOString(),
    };
    const published = await mqttWorker.publishCommand(room.id, 'sync', payload);
    if (published) {
      publishedCount += 1;
    }
    await recordAuditEvent(room.id, 'lecturer_sync', {
      action: 'requested',
      scope: 'all_rooms',
      total_count: lecturers.length,
      mqtt_published: published,
    });
  }

  res.status(publishedCount > 0 ? 202 : 503).json({
    published: publishedCount,
    total_rooms: rooms.length,
    total_count: lecturers.length,
    error: publishedCount > 0 ? undefined : 'MQTT not connected',
  });
}));

app.post('/api/rooms/:roomId/sync', asyncHandler(async (req, res) => {
  const config = await getRoomConfig(req.params.roomId);
  if (!config.room) {
    res.status(404).json({ error: 'room not found' });
    return;
  }

  const lecturers = await listLecturers();
  const payload = {
    command: 'sync_lecturers',
    room_id: req.params.roomId,
    lecturers,
    total_count: lecturers.length,
    timestamp: new Date().toISOString(),
  };
  const published = await mqttWorker.publishCommand(req.params.roomId, 'sync', payload);
  await recordAuditEvent(req.params.roomId, 'lecturer_sync', {
    action: 'requested',
    scope: 'room',
    total_count: lecturers.length,
    mqtt_published: published,
  });
  res.status(published ? 202 : 503).json({
    published,
    total_count: lecturers.length,
    error: published ? undefined : 'MQTT not connected',
  });
}));

app.get('/api/ota', asyncHandler(async (req, res) => {
  await fs.mkdir(OTA_DIR, { recursive: true });
  const files = await fs.readdir(OTA_DIR);
  res.json({
    ota_dir: OTA_DIR,
    files: await Promise.all(files.map(async (file) => {
      const stats = await fs.stat(path.join(OTA_DIR, file));
      return {
        file,
        size: stats.size,
        uploaded_at: stats.mtime.toISOString(),
        url: `/ota/${encodeURIComponent(file)}`,
      };
    })),
  });
}));

app.post('/api/ota/upload', asyncHandler(async (req, res) => {
  await fs.mkdir(OTA_DIR, { recursive: true });
  const upload = await parseFirmwareUpload(req);
  try {
    await fs.writeFile(upload.target, upload.buffer, { flag: 'wx' });
  } catch (err) {
    if (err.code === 'EEXIST') {
      throw httpError('firmware file already exists, retry upload', 409);
    }
    throw err;
  }

  const result = {
    file: upload.fileName,
    file_name: upload.fileName,
    size: upload.buffer.length,
    url: `/ota/${encodeURIComponent(upload.fileName)}`,
    uploaded_at: upload.uploadedAt,
  };

  await recordAuditEvent('server', 'ota_upload', {
    action: 'uploaded',
    file: result.file,
    size: result.size,
    url: result.url,
  });

  res.status(201).json(result);
}));

app.post('/api/rooms/:roomId/ota', asyncHandler(async (req, res) => {
  const config = await getRoomConfig(req.params.roomId);
  if (!config.room) {
    res.status(404).json({ error: 'room not found' });
    return;
  }

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
  await recordAuditEvent(req.params.roomId, 'ota_request', {
    action: 'requested',
    version,
    url,
    checksum,
    mqtt_published: published,
  });
  res.status(published ? 202 : 503).json({
    published,
    error: published ? undefined : 'MQTT not connected',
  });
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
