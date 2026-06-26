const fs = require('fs/promises');
const path = require('path');
const zlib = require('zlib');

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
  markDeviceCommandPublishFailed,
  markDeviceCommandRequested,
  recordEvent,
  upsertLecturer,
} = require('./db');
const { createMqttWorker } = require('./mqttWorker');

const app = express();
const PORT = Number(process.env.PORT || 3000);
const OTA_DIR = process.env.OTA_DIR || path.join(__dirname, '..', 'ota');
const UPDATE_STAGING_DIR = process.env.UPDATE_STAGING_DIR || path.join(OTA_DIR, 'staging');
const UPDATE_MAX_UPLOAD_BYTES = Number(process.env.UPDATE_MAX_UPLOAD_BYTES || 512 * 1024 * 1024);
const DASHBOARD_DIR = process.env.DASHBOARD_DIR
  || path.join(__dirname, '..', 'webdashboard', 'public');

const UPDATE_STATUS = Object.freeze({
  UPLOADED: 'uploaded',
  VALIDATING: 'validating',
  INSTALLING: 'installing',
  RESTARTING: 'restarting',
  SUCCESS: 'success',
  FAILED: 'failed',
  ROLLBACK_DONE: 'rollback_done',
});

const PACKAGE_TYPES = Object.freeze({
  mcu_firmware: {
    target: 'mcu',
    extensions: ['.bin'],
    maxBytes: 64 * 1024 * 1024,
  },
  linuxapp_binary: {
    target: 'sbc',
    extensions: ['.elf', ''],
    maxBytes: 128 * 1024 * 1024,
    requiresComponent: true,
    requiresElf: true,
  },
  config_update: {
    target: 'sbc',
    extensions: ['.json', '.tar', '.tar.gz', '.tgz', '.zip'],
    maxBytes: 32 * 1024 * 1024,
  },
  release_bundle: {
    target: 'sbc',
    extensions: ['.tar.gz', '.tgz'],
    maxBytes: 512 * 1024 * 1024,
    requiresManifest: true,
    requiredArchiveRoots: ['bin/', 'config/', 'systemd/'],
  },
});

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

function getUploadedFile(parts) {
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

function getMultipartFields(parts) {
  const fields = {};
  for (const part of parts) {
    const disposition = part.headers['content-disposition'];
    if (!disposition || !/form-data/i.test(disposition)) continue;

    const params = parseHeaderParams(disposition);
    if (!params.name || params.filename) continue;
    fields[params.name] = part.body.toString('utf8').trim();
  }
  return fields;
}

function artifactExtension(fileName) {
  const lower = String(fileName || '').toLowerCase();
  if (lower.endsWith('.tar.gz')) return '.tar.gz';
  if (lower.endsWith('.tgz')) return '.tgz';
  return path.extname(lower);
}

function stripArtifactExtension(fileName) {
  const extension = artifactExtension(fileName);
  return extension ? fileName.slice(0, -extension.length) : fileName;
}

function safeMetadataValue(value, fallback = '') {
  return String(value || fallback)
    .trim()
    .replace(/[^A-Za-z0-9._-]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 80);
}

async function uniqueUpdateFileName(fileName, uploadedAt) {
  const extension = artifactExtension(fileName);
  const base = stripArtifactExtension(path.basename(fileName));
  let candidate = fileName;

  for (let index = 0; index < 100; index += 1) {
    const target = path.resolve(UPDATE_STAGING_DIR, candidate);
    try {
      await fs.access(target);
      const suffix = index === 0 ? compactTimestamp(uploadedAt) : `${compactTimestamp(uploadedAt)}-${index}`;
      candidate = `${base}-${suffix}${extension}`;
    } catch {
      return { fileName: candidate, target };
    }
  }

  throw httpError('could not allocate unique update file name', 409);
}

function normalizeChecksum(value) {
  const checksum = String(value || '').trim().toLowerCase().replace(/^sha256:/, '');
  if (!checksum) return '';
  if (!/^[a-f0-9]{64}$/.test(checksum)) {
    throw httpError('checksum must be a sha256 hex digest');
  }
  return checksum;
}

function sha256(buffer) {
  return require('crypto').createHash('sha256').update(buffer).digest('hex');
}

function isElf(buffer) {
  return buffer.length >= 4
    && buffer[0] === 0x7f
    && buffer[1] === 0x45
    && buffer[2] === 0x4c
    && buffer[3] === 0x46;
}

function readTarEntries(buffer, compressed) {
  const tarBuffer = compressed ? zlib.gunzipSync(buffer) : buffer;
  const entries = [];
  let offset = 0;

  while (offset + 512 <= tarBuffer.length) {
    const header = tarBuffer.slice(offset, offset + 512);
    if (header.every((byte) => byte === 0)) break;

    const name = header.slice(0, 100).toString('utf8').replace(/\0.*$/, '');
    const prefix = header.slice(345, 500).toString('utf8').replace(/\0.*$/, '');
    const fullName = [prefix, name].filter(Boolean).join('/');
    const sizeText = header.slice(124, 136).toString('utf8').replace(/\0.*$/, '').trim();
    const size = parseInt(sizeText || '0', 8);
    const dataOffset = offset + 512;

    if (fullName) {
      entries.push({
        name: fullName.replace(/^\.\/+/, ''),
        data: tarBuffer.slice(dataOffset, dataOffset + size),
      });
    }

    offset = dataOffset + Math.ceil(size / 512) * 512;
  }

  return entries;
}

function readZipEntries(buffer) {
  const entries = [];
  const eocdSignature = 0x06054b50;
  let eocdOffset = -1;

  for (let index = buffer.length - 22; index >= 0; index -= 1) {
    if (buffer.readUInt32LE(index) === eocdSignature) {
      eocdOffset = index;
      break;
    }
  }

  if (eocdOffset === -1) {
    throw httpError('invalid zip package');
  }

  const entryCount = buffer.readUInt16LE(eocdOffset + 10);
  const centralDirectoryOffset = buffer.readUInt32LE(eocdOffset + 16);
  let cursor = centralDirectoryOffset;

  for (let index = 0; index < entryCount; index += 1) {
    if (buffer.readUInt32LE(cursor) !== 0x02014b50) {
      throw httpError('invalid zip central directory');
    }

    const compressionMethod = buffer.readUInt16LE(cursor + 10);
    const compressedSize = buffer.readUInt32LE(cursor + 20);
    const fileNameLength = buffer.readUInt16LE(cursor + 28);
    const extraLength = buffer.readUInt16LE(cursor + 30);
    const commentLength = buffer.readUInt16LE(cursor + 32);
    const localHeaderOffset = buffer.readUInt32LE(cursor + 42);
    const name = buffer.slice(cursor + 46, cursor + 46 + fileNameLength).toString('utf8');

    if (buffer.readUInt32LE(localHeaderOffset) !== 0x04034b50) {
      throw httpError('invalid zip local header');
    }

    const localNameLength = buffer.readUInt16LE(localHeaderOffset + 26);
    const localExtraLength = buffer.readUInt16LE(localHeaderOffset + 28);
    const dataOffset = localHeaderOffset + 30 + localNameLength + localExtraLength;
    const compressedData = buffer.slice(dataOffset, dataOffset + compressedSize);
    let data = Buffer.alloc(0);

    if (!name.endsWith('/')) {
      if (compressionMethod === 0) {
        data = compressedData;
      } else if (compressionMethod === 8) {
        data = zlib.inflateRawSync(compressedData);
      } else {
        throw httpError(`unsupported zip compression method ${compressionMethod}`);
      }
    }

    entries.push({ name: name.replace(/^\.\/+/, ''), data });
    cursor += 46 + fileNameLength + extraLength + commentLength;
  }

  return entries;
}

function inspectArchive(buffer, extension) {
  if (extension === '.tar') {
    return readTarEntries(buffer, false);
  }
  if (extension === '.tar.gz' || extension === '.tgz') {
    return readTarEntries(buffer, true);
  }
  if (extension === '.zip') {
    return readZipEntries(buffer);
  }
  return [];
}

function validateManifest(buffer, extension, packageType) {
  const entries = inspectArchive(buffer, extension);
  if (entries.some((entry) => path.isAbsolute(entry.name) || entry.name.split('/').includes('..'))) {
    throw httpError('package archive contains unsafe paths');
  }

  const manifestEntry = entries.find((entry) => entry.name === 'manifest.json' || entry.name.endsWith('/manifest.json'));
  if (!manifestEntry) {
    throw httpError('package manifest.json is required');
  }

  let manifest;
  try {
    manifest = JSON.parse(manifestEntry.data.toString('utf8'));
  } catch {
    throw httpError('manifest.json must be valid JSON');
  }

  const definition = PACKAGE_TYPES[packageType];
  for (const root of definition.requiredArchiveRoots || []) {
    if (!entries.some((entry) => entry.name.startsWith(root) || entry.name.includes(`/${root}`))) {
      throw httpError(`release bundle must contain ${root}`);
    }
  }

  return { manifest, entries: entries.map((entry) => entry.name) };
}

function normalizeUpdateMetadata(fields, fileName) {
  const packageType = String(fields.package_type || fields.packageType || '').trim();
  if (!PACKAGE_TYPES[packageType]) {
    throw httpError(`package_type must be one of ${Object.keys(PACKAGE_TYPES).join(', ')}`);
  }

  const definition = PACKAGE_TYPES[packageType];
  const target = String(fields.target || definition.target).trim().toLowerCase();
  if (target !== definition.target) {
    throw httpError(`${packageType} updates must target ${definition.target}`);
  }

  const component = safeMetadataValue(fields.component);
  if (definition.requiresComponent && !component) {
    throw httpError('component is required for linuxapp_binary updates');
  }

  return {
    package_type: packageType,
    target,
    component,
    version: String(fields.version || '').trim(),
    architecture: safeMetadataValue(fields.architecture, 'unknown'),
    checksum: normalizeChecksum(fields.checksum),
    original_name: fileName,
  };
}

function validateUpdateArtifact(metadata, buffer) {
  const definition = PACKAGE_TYPES[metadata.package_type];
  const extension = artifactExtension(metadata.original_name);

  if (!definition.extensions.includes(extension)) {
    const readable = definition.extensions.map((value) => value || 'no extension').join(', ');
    throw httpError(`${metadata.package_type} requires ${readable}`);
  }

  if (!buffer.length) {
    throw httpError('update file is empty');
  }

  if (buffer.length > definition.maxBytes) {
    throw httpError(`${metadata.package_type} upload exceeds ${definition.maxBytes} bytes`, 413);
  }

  const actualChecksum = sha256(buffer);
  if (metadata.checksum && metadata.checksum !== actualChecksum) {
    throw httpError('checksum mismatch');
  }

  if (definition.requiresElf && !isElf(buffer)) {
    throw httpError('linuxapp_binary must be an ELF executable');
  }

  let manifestInfo = null;
  if (extension === '.json') {
    try {
      JSON.parse(buffer.toString('utf8'));
    } catch {
      throw httpError('config_update JSON must be valid JSON');
    }
  } else if (definition.requiresManifest || ['.tar', '.tar.gz', '.tgz', '.zip'].includes(extension)) {
    manifestInfo = validateManifest(buffer, extension, metadata.package_type);
  }

  return {
    extension,
    checksum: actualChecksum,
    manifest: manifestInfo?.manifest,
    archive_entries: manifestInfo?.entries,
  };
}

async function parseUpdateUpload(req) {
  const boundary = parseMultipartBoundary(req.headers['content-type']);
  if (!boundary) {
    throw httpError('multipart/form-data boundary is required');
  }

  const parts = parseMultipart(await readRequestBuffer(req, UPDATE_MAX_UPLOAD_BYTES), boundary);
  const upload = getUploadedFile(parts);
  if (!upload) {
    throw httpError('update file is required');
  }

  const originalName = String(upload.originalName || '').trim();
  if (!originalName || /[\\/]/.test(originalName)) {
    throw httpError('invalid update file name');
  }

  const fileName = path.basename(originalName);
  const metadata = normalizeUpdateMetadata(getMultipartFields(parts), fileName);
  const validation = validateUpdateArtifact(metadata, upload.buffer);

  const uploadedAt = new Date().toISOString();
  const prefix = [
    metadata.package_type,
    metadata.target,
    metadata.component,
    metadata.version,
  ].filter(Boolean).map((value) => safeMetadataValue(value)).join('-');
  const storedBase = prefix ? `${prefix}-${fileName}` : fileName;
  const { fileName: storedName, target } = await uniqueUpdateFileName(storedBase, uploadedAt);
  const stagingRoot = path.resolve(UPDATE_STAGING_DIR);
  if (!target.startsWith(`${stagingRoot}${path.sep}`)) {
    throw httpError('invalid update path');
  }

  return {
    fileName: storedName,
    target,
    buffer: upload.buffer,
    uploadedAt,
    metadata: {
      ...metadata,
      ...validation,
      file_name: storedName,
      size: upload.buffer.length,
      uploaded_at: uploadedAt,
      status: UPDATE_STATUS.SUCCESS,
      status_history: [
        UPDATE_STATUS.UPLOADED,
        UPDATE_STATUS.VALIDATING,
        UPDATE_STATUS.SUCCESS,
      ],
    },
  };
}

async function readMetadataForFile(filePath) {
  try {
    const data = await fs.readFile(`${filePath}.metadata.json`, 'utf8');
    return JSON.parse(data);
  } catch {
    return {};
  }
}

async function listUpdateFiles(rootDir, relativeDir = '') {
  const absoluteDir = path.join(rootDir, relativeDir);
  let entries;
  try {
    entries = await fs.readdir(absoluteDir, { withFileTypes: true });
  } catch {
    return [];
  }

  const files = [];
  for (const entry of entries) {
    const relativePath = path.join(relativeDir, entry.name);
    const absolutePath = path.join(rootDir, relativePath);

    if (entry.isDirectory()) {
      files.push(...await listUpdateFiles(rootDir, relativePath));
      continue;
    }
    if (entry.name.endsWith('.metadata.json')) continue;

    const stats = await fs.stat(absolutePath);
    const metadata = await readMetadataForFile(absolutePath);
    const url = `/${['ota', ...relativePath.split(path.sep).map(encodeURIComponent)].join('/')}`;
    files.push({
      file: relativePath,
      file_name: path.basename(relativePath),
      size: stats.size,
      uploaded_at: stats.mtime.toISOString(),
      url,
      ...metadata,
    });
  }

  return files.sort((a, b) => String(b.uploaded_at).localeCompare(String(a.uploaded_at)));
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

  if (!published) {
    await recordAuditEvent(req.params.roomId, 'device_control', {
      action: 'publish_failed',
      requested_state: state,
      mqtt_published: false,
      device_id: device.id,
      device_type: device.type,
      protocol: device.protocol,
    });
    await markDeviceCommandPublishFailed(req.params.roomId, device.id, state);
    res.status(503).json({
      error: 'MQTT not connected or publish failed',
      published: false,
    });
    return;
  }

  await recordAuditEvent(req.params.roomId, 'device_control', {
    action: 'requested',
    requested_state: state,
    mqtt_published: true,
    device_id: device.id,
    device_type: device.type,
    protocol: device.protocol,
  });
  await markDeviceCommandRequested(req.params.roomId, device.id, state);
  res.status(202).json({
    published: true,
    command_state: 'requested',
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
  await fs.mkdir(UPDATE_STAGING_DIR, { recursive: true });
  res.json({
    ota_dir: OTA_DIR,
    update_dir: OTA_DIR,
    staging_dir: UPDATE_STAGING_DIR,
    files: await listUpdateFiles(OTA_DIR),
  });
}));

app.get('/api/updates', asyncHandler(async (req, res) => {
  await fs.mkdir(UPDATE_STAGING_DIR, { recursive: true });
  res.json({
    update_dir: OTA_DIR,
    staging_dir: UPDATE_STAGING_DIR,
    files: await listUpdateFiles(OTA_DIR),
  });
}));

async function handleUpdateUpload(req, res) {
  await fs.mkdir(UPDATE_STAGING_DIR, { recursive: true });
  const upload = await parseUpdateUpload(req);
  try {
    await fs.writeFile(upload.target, upload.buffer, { flag: 'wx' });
    await fs.writeFile(
      `${upload.target}.metadata.json`,
      JSON.stringify(upload.metadata, null, 2),
      { flag: 'wx' },
    );
  } catch (err) {
    if (err.code === 'EEXIST') {
      throw httpError('update file already exists, retry upload', 409);
    }
    throw err;
  }

  const result = {
    file: upload.fileName,
    file_name: upload.fileName,
    size: upload.buffer.length,
    url: `/ota/staging/${encodeURIComponent(upload.fileName)}`,
    uploaded_at: upload.uploadedAt,
    ...upload.metadata,
  };

  await recordAuditEvent('server', 'update_upload', {
    action: UPDATE_STATUS.SUCCESS,
    status: UPDATE_STATUS.SUCCESS,
    status_history: upload.metadata.status_history,
    package_type: result.package_type,
    target: result.target,
    component: result.component,
    version: result.version,
    architecture: result.architecture,
    file: result.file,
    size: result.size,
    url: result.url,
    checksum: result.checksum,
  });

  res.status(201).json(result);
}

app.post('/api/ota/upload', asyncHandler(handleUpdateUpload));

app.post('/api/updates/upload', asyncHandler(handleUpdateUpload));

function absoluteUpdateUrl(req, url) {
  if (/^https?:\/\//i.test(url)) return url;
  if (!url.startsWith('/')) return url;

  const protocol = req.headers['x-forwarded-proto'] || req.protocol || 'http';
  const host = req.headers['x-forwarded-host'] || req.headers.host;
  return host ? `${protocol}://${host}${url}` : url;
}

function normalizeOtaRequest(req, body) {
  const packageType = String(body.package_type || body.packageType || 'mcu_firmware').trim();
  if (!PACKAGE_TYPES[packageType]) {
    throw httpError(`package_type must be one of ${Object.keys(PACKAGE_TYPES).join(', ')}`);
  }

  const definition = PACKAGE_TYPES[packageType];
  const target = String(body.target || definition.target).trim().toLowerCase();
  if (target !== definition.target) {
    throw httpError(`${packageType} updates must target ${definition.target}`);
  }

  const version = String(body.version || '').trim();
  const url = String(body.url || '').trim();
  if (!version || !url) {
    throw httpError('version and url are required');
  }

  return {
    package_type: packageType,
    target,
    component: safeMetadataValue(body.component),
    version,
    architecture: safeMetadataValue(body.architecture, 'unknown'),
    url: absoluteUpdateUrl(req, url),
    checksum: normalizeChecksum(body.checksum),
    file_name: String(body.file_name || body.file || '').trim(),
    timestamp: new Date().toISOString(),
  };
}

app.post('/api/rooms/:roomId/ota', asyncHandler(async (req, res) => {
  const config = await getRoomConfig(req.params.roomId);
  if (!config.room) {
    res.status(404).json({ error: 'room not found' });
    return;
  }

  const payload = normalizeOtaRequest(req, req.body);

  const published = await mqttWorker.publishCommand(req.params.roomId, 'ota', payload);
  await recordAuditEvent(req.params.roomId, 'ota_request', {
    action: 'requested',
    status: UPDATE_STATUS.UPLOADED,
    ...payload,
    mqtt_published: published,
  });
  res.status(published ? 202 : 503).json({
    published,
    payload,
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
