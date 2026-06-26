const { Pool } = require('pg');

const pool = new Pool({
  host: process.env.DB_HOST || 'localhost',
  port: Number(process.env.DB_PORT || 5432),
  user: process.env.DB_USER || 'sps',
  password: process.env.DB_PASSWORD || 'sps_secret',
  database: process.env.DB_NAME || 'sps_db',
});

const RUNTIME_STATUS_WINDOW_MS = Number(process.env.RUNTIME_STATUS_WINDOW_MS || 5 * 60 * 1000);

const LEGACY_DEMO_ROOM_IDS = ['room101', 'room102', 'lab201'];

const PERIPHERAL_MAP = {
  'projector RS232': { type: 'projector', protocol: 'rs232', name: 'Projector RS232' },
  light: { type: 'light', protocol: 'relay', name: 'Light relay' },
  curtain: { type: 'curtain', protocol: 'relay', name: 'Curtain relay' },
  screen: { type: 'screen', protocol: 'relay', name: 'Screen relay' },
  'AC IR': { type: 'ac', protocol: 'ir', name: 'AC IR' },
  'PC WoL': { type: 'pc', protocol: 'wol', name: 'PC Wake-on-LAN' },
};

const VIRTUAL_CLASSROOMS = [
  {
    room_code: 'A01.01',
    pcd_code: 'PCD-SPS-A0101',
    static_ip: '192.168.2.2',
    declared_status: 'Active',
    peripherals: ['projector RS232', 'light', 'curtain', 'screen', 'AC IR', 'PC WoL'],
  },
  {
    room_code: 'A01.02',
    pcd_code: 'PCD-SPS-A0102',
    static_ip: '192.168.1.12',
    declared_status: 'Active',
    peripherals: ['projector RS232', 'light', 'screen', 'AC IR', 'PC WoL'],
  },
  {
    room_code: 'A02.01',
    pcd_code: 'PCD-SPS-A0201',
    static_ip: '192.168.1.21',
    declared_status: 'Idle',
    peripherals: ['projector RS232', 'light', 'curtain', 'AC IR', 'PC WoL'],
  },
  {
    room_code: 'B01.01',
    pcd_code: 'PCD-SPS-B0101',
    static_ip: '192.168.2.11',
    declared_status: 'Active',
    peripherals: ['projector RS232', 'light', 'curtain', 'screen', 'AC IR', 'PC WoL'],
  },
  {
    room_code: 'B01.02',
    pcd_code: 'PCD-SPS-B0102',
    static_ip: '192.168.2.12',
    declared_status: 'Maintenance',
    peripherals: ['projector RS232', 'light', 'screen', 'AC IR'],
  },
  {
    room_code: 'B03.05',
    pcd_code: 'PCD-SPS-B0305',
    static_ip: '192.168.2.35',
    declared_status: 'Active',
    peripherals: ['projector RS232', 'light', 'curtain', 'screen', 'AC IR', 'PC WoL'],
  },
  {
    room_code: 'C01.01',
    pcd_code: 'PCD-SPS-C0101',
    static_ip: '192.168.3.11',
    declared_status: 'Offline',
    peripherals: ['projector RS232', 'light', 'curtain', 'screen', 'AC IR', 'PC WoL'],
  },
  {
    room_code: 'C04.02',
    pcd_code: 'PCD-SPS-C0402',
    static_ip: '192.168.3.42',
    declared_status: 'Active',
    peripherals: ['projector RS232', 'light', 'AC IR', 'PC WoL'],
  },
  {
    room_code: 'D02.03',
    pcd_code: 'PCD-SPS-D0203',
    static_ip: '192.168.4.23',
    declared_status: 'Active',
    peripherals: ['projector RS232', 'light', 'curtain', 'screen', 'AC IR', 'PC WoL'],
  },
  {
    room_code: 'E05.10',
    pcd_code: 'PCD-SPS-E0510',
    static_ip: '192.168.5.10',
    declared_status: 'Active',
    peripherals: ['projector RS232', 'light', 'curtain', 'screen', 'AC IR', 'PC WoL'],
  },
];

function roomLocation(roomCode) {
  const building = roomCode.slice(0, 1);
  const floor = roomCode.slice(1, 3);
  return `Building ${building} - Floor ${floor}`;
}

function normalizeDeclaredStatus(value) {
  const status = String(value || '').trim().toLowerCase();
  if (status === 'active') return 'Active';
  if (status === 'idle') return 'Idle';
  if (status === 'maintenance') return 'Maintenance';
  if (status === 'offline') return 'Offline';
  if (status === 'warning') return 'Warning';
  return 'Unknown';
}

function normalizeRuntimeStatus(value) {
  const status = String(value || '').trim().toLowerCase();
  if (['connected', 'online', 'active', 'heartbeat', 'ok', 'healthy', 'ready'].includes(status)) {
    return 'Active';
  }
  if (['idle', 'standby'].includes(status)) return 'Idle';
  if (['maintenance', 'maint'].includes(status)) return 'Maintenance';
  if (['offline', 'disconnected', 'down', 'lost'].includes(status)) return 'Offline';
  if (['warning', 'warn', 'degraded', 'error', 'failed', 'failure'].includes(status)) return 'Warning';
  return null;
}

function normalizeDeviceState(value) {
  const state = String(value || '').trim().toLowerCase();
  if (['on', 'true', '1', 'enabled', 'open', 'up', 'running'].includes(state)) return 'on';
  if (['off', 'false', '0', 'disabled', 'closed', 'down', 'stopped'].includes(state)) return 'off';
  return null;
}

function payloadValue(payload, keys) {
  if (!payload || typeof payload !== 'object') return null;

  for (const key of keys) {
    const parts = key.split('.');
    let current = payload;
    for (const part of parts) {
      if (!current || typeof current !== 'object' || !(part in current)) {
        current = null;
        break;
      }
      current = current[part];
    }

    if (current !== null && current !== undefined && current !== '') {
      return current;
    }
  }

  return null;
}

function eventMatches(event, patterns) {
  const value = `${event.category || ''}/${event.type || ''}/${event.topic || ''}`.toLowerCase();
  return patterns.some((pattern) => value.includes(pattern));
}

function isMqttRuntimeEvent(event) {
  return ['status', 'event'].includes(event.category);
}

function isErrorEvent(event) {
  const payload = event.payload || {};
  const level = String(payload.level || payload.severity || '').toLowerCase();
  return eventMatches(event, ['error', 'fault', 'fail'])
    || Boolean(payload.error)
    || level === 'error'
    || level === 'critical';
}

function errorMessage(event) {
  if (!isErrorEvent(event)) return null;

  const payload = event.payload || {};
  return String(
    payload.error
      || payload.message
      || payload.reason
      || payload.status
      || event.type
      || 'Error'
  );
}

function deriveStatusFromEvent(event) {
  if (!event) return null;
  if (isErrorEvent(event)) return 'Warning';

  const payload = event.payload || {};
  const statusValue = payloadValue(payload, [
    'runtime_status',
    'runtimeStatus',
    'declared_status',
    'declaredStatus',
    'connection_status',
    'connectionStatus',
    'status',
    'state',
  ]);
  const normalized = normalizeRuntimeStatus(statusValue);
  if (normalized) return normalized;

  if (eventMatches(event, ['connection', 'heartbeat', 'devices', 'status'])) {
    return 'Active';
  }

  return null;
}

function latestPayloadValue(events, keys, predicate = () => true) {
  const event = events.find((item) => predicate(item) && payloadValue(item.payload, keys));
  return event ? String(payloadValue(event.payload, keys)) : null;
}

function latestWorkflowStatus(events, patterns, auditType) {
  const event = events.find((item) => (
    eventMatches(item, patterns)
      || (auditType && item.category === 'audit' && item.type === auditType)
  ));

  if (!event) return null;

  const status = payloadValue(event.payload, ['status', 'state', 'result', 'action']);
  if (status) return String(status);

  return event.category === 'audit' ? 'requested' : 'observed';
}

function summarizeOverviewRow(room, events) {
  const runtimeEvents = events.filter(isMqttRuntimeEvent);
  const latestRuntimeEvent = runtimeEvents[0] || null;
  const latestErrorEvent = events.find(isErrorEvent);
  const latestRuntimeStatus = deriveStatusFromEvent(latestRuntimeEvent);
  const runtimeAge = latestRuntimeEvent
    ? Date.now() - new Date(latestRuntimeEvent.created_at).getTime()
    : null;

  let derivedRuntimeStatus = normalizeDeclaredStatus(room.declared_status);
  let runtimeConfidence = room.declared_status ? 'seeded' : 'unknown';

  if (latestRuntimeStatus && runtimeAge !== null && runtimeAge <= RUNTIME_STATUS_WINDOW_MS) {
    derivedRuntimeStatus = latestRuntimeStatus;
    runtimeConfidence = 'observed';
  } else if (latestRuntimeEvent) {
    runtimeConfidence = 'stale';
  }

  return {
    ...room,
    declared_status: normalizeDeclaredStatus(room.declared_status),
    derived_runtime_status: derivedRuntimeStatus,
    runtime_confidence: runtimeConfidence,
    last_seen_at: latestRuntimeEvent ? latestRuntimeEvent.created_at : null,
    firmware_version: latestPayloadValue(events, [
      'firmware_version',
      'firmwareVersion',
      'firmware.version',
      'current_version',
      'currentVersion',
    ], isMqttRuntimeEvent),
    lecturer_sync_status: latestWorkflowStatus(events, ['dbsync', 'sync'], 'lecturer_sync'),
    ota_status: latestWorkflowStatus(events, ['ota'], 'ota_request'),
    latest_error: latestErrorEvent ? errorMessage(latestErrorEvent) : null,
  };
}

async function ensureSchema() {
  await pool.query(`
    CREATE TABLE IF NOT EXISTS lecturers (
      id TEXT PRIMARY KEY,
      name TEXT NOT NULL,
      rfid TEXT NOT NULL UNIQUE,
      authorized BOOLEAN NOT NULL DEFAULT TRUE,
      updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
    );

    CREATE TABLE IF NOT EXISTS rooms (
      id TEXT PRIMARY KEY,
      name TEXT NOT NULL,
      location TEXT,
      room_code TEXT,
      pcd_code TEXT,
      static_ip TEXT,
      declared_status TEXT,
      updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
    );

    CREATE TABLE IF NOT EXISTS devices (
      id TEXT PRIMARY KEY,
      room_id TEXT NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
      name TEXT NOT NULL,
      type TEXT NOT NULL,
      protocol TEXT NOT NULL DEFAULT 'unknown',
      state TEXT NOT NULL DEFAULT 'unknown',
      metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
      updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
    );

    CREATE TABLE IF NOT EXISTS events (
      id BIGSERIAL PRIMARY KEY,
      room_id TEXT NOT NULL,
      category TEXT NOT NULL,
      type TEXT NOT NULL,
      topic TEXT NOT NULL,
      payload JSONB NOT NULL DEFAULT '{}'::jsonb,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
    );
  `);

  await pool.query(`
    ALTER TABLE rooms ADD COLUMN IF NOT EXISTS room_code TEXT;
    ALTER TABLE rooms ADD COLUMN IF NOT EXISTS pcd_code TEXT;
    ALTER TABLE rooms ADD COLUMN IF NOT EXISTS static_ip TEXT;
    ALTER TABLE rooms ADD COLUMN IF NOT EXISTS declared_status TEXT;
    ALTER TABLE devices ADD COLUMN IF NOT EXISTS protocol TEXT NOT NULL DEFAULT 'unknown';
    ALTER TABLE devices ADD COLUMN IF NOT EXISTS requested_state TEXT;
    ALTER TABLE devices ADD COLUMN IF NOT EXISTS command_status TEXT DEFAULT 'idle';
    ALTER TABLE devices ADD COLUMN IF NOT EXISTS command_requested_at TIMESTAMPTZ;
    ALTER TABLE devices ADD COLUMN IF NOT EXISTS state_confirmed_at TIMESTAMPTZ;
    CREATE INDEX IF NOT EXISTS idx_events_room_created_at ON events (room_id, created_at DESC);
    CREATE INDEX IF NOT EXISTS idx_events_created_at ON events (created_at DESC);
  `);

  await seedVirtualClassrooms();
}

async function seedVirtualClassrooms() {
  await pool.query('DELETE FROM rooms WHERE id = ANY($1)', [LEGACY_DEMO_ROOM_IDS]);

  for (const room of VIRTUAL_CLASSROOMS) {
    const roomId = room.room_code;
    await pool.query(
      `INSERT INTO rooms (id, name, location, room_code, pcd_code, static_ip, declared_status, updated_at)
       VALUES ($1, $2, $3, $4, $5, $6, $7, NOW())
       ON CONFLICT (id)
       DO UPDATE SET name = EXCLUDED.name,
                     location = EXCLUDED.location,
                     room_code = EXCLUDED.room_code,
                     pcd_code = EXCLUDED.pcd_code,
                     static_ip = EXCLUDED.static_ip,
                     declared_status = EXCLUDED.declared_status,
                     updated_at = NOW()`,
      [
        roomId,
        `Room ${room.room_code}`,
        roomLocation(room.room_code),
        room.room_code,
        room.pcd_code,
        room.static_ip,
        normalizeDeclaredStatus(room.declared_status),
      ]
    );

    const deviceIds = [];
    for (const [index, peripheral] of room.peripherals.entries()) {
      const mapped = PERIPHERAL_MAP[peripheral];
      const deviceId = `${room.room_code}-${mapped.type}`;
      deviceIds.push(deviceId);
      await pool.query(
        `INSERT INTO devices (id, room_id, name, type, protocol, state, metadata, updated_at)
         VALUES ($1, $2, $3, $4, $5, 'unknown', $6, NOW())
         ON CONFLICT (id)
         DO UPDATE SET room_id = EXCLUDED.room_id,
                       name = EXCLUDED.name,
                       type = EXCLUDED.type,
                       protocol = EXCLUDED.protocol,
                       metadata = EXCLUDED.metadata,
                       updated_at = NOW()`,
        [
          deviceId,
          roomId,
          mapped.name,
          mapped.type,
          mapped.protocol,
          {
            label: String(index + 1).padStart(2, '0'),
            registered_peripheral: peripheral,
            protocol: mapped.protocol,
            channel: mapped.protocol === 'relay' ? index + 1 : null,
          },
        ]
      );
    }

    await pool.query(
      'DELETE FROM devices WHERE room_id = $1 AND NOT (id = ANY($2))',
      [roomId, deviceIds]
    );
  }
}

async function dbHealth() {
  await pool.query('SELECT 1');
  return 'ok';
}

async function listLecturers() {
  const result = await pool.query(
    'SELECT id, name, rfid, authorized FROM lecturers ORDER BY id'
  );
  return result.rows;
}

async function listRooms() {
  const result = await pool.query(
    `SELECT r.id,
            r.name,
            r.location,
            COALESCE(r.room_code, r.id) AS room_code,
            r.pcd_code,
            r.static_ip,
            COALESCE(r.declared_status, 'Unknown') AS declared_status,
            COUNT(d.id)::int AS device_count,
            COUNT(*) FILTER (WHERE d.state = 'on')::int AS active_device_count
     FROM rooms r
     LEFT JOIN devices d ON d.room_id = r.id
     GROUP BY r.id
     ORDER BY COALESCE(r.room_code, r.id)`
  );
  return result.rows;
}

async function listOverview() {
  const rooms = await listRooms();
  if (!rooms.length) {
    return [];
  }

  const roomIds = rooms.map((room) => room.id);
  const eventsResult = await pool.query(
    `SELECT id, room_id, category, type, topic, payload, created_at
     FROM (
       SELECT e.*,
              ROW_NUMBER() OVER (PARTITION BY room_id ORDER BY created_at DESC) AS rn
       FROM events e
       WHERE room_id = ANY($1)
     ) ranked
     WHERE rn <= 50
     ORDER BY room_id, created_at DESC`,
    [roomIds]
  );

  const eventsByRoom = new Map();
  for (const event of eventsResult.rows) {
    if (!eventsByRoom.has(event.room_id)) {
      eventsByRoom.set(event.room_id, []);
    }
    eventsByRoom.get(event.room_id).push(event);
  }

  return rooms.map((room) => summarizeOverviewRow(room, eventsByRoom.get(room.id) || []));
}

async function upsertLecturer(lecturer) {
  const result = await pool.query(
    `INSERT INTO lecturers (id, name, rfid, authorized, updated_at)
     VALUES ($1, $2, $3, $4, NOW())
     ON CONFLICT (id)
     DO UPDATE SET name = EXCLUDED.name,
                   rfid = EXCLUDED.rfid,
                   authorized = EXCLUDED.authorized,
                   updated_at = NOW()
     RETURNING id, name, rfid, authorized`,
    [lecturer.id, lecturer.name, lecturer.rfid, lecturer.authorized]
  );
  return result.rows[0];
}

async function deleteLecturer(id) {
  const result = await pool.query(
    'DELETE FROM lecturers WHERE id = $1 RETURNING id',
    [id]
  );
  return result.rowCount > 0;
}

async function getRoomConfig(roomId) {
  const roomResult = await pool.query(
    `SELECT id,
            name,
            location,
            COALESCE(room_code, id) AS room_code,
            pcd_code,
            static_ip,
            COALESCE(declared_status, 'Unknown') AS declared_status
     FROM rooms
     WHERE id = $1`,
    [roomId]
  );
  const deviceResult = await pool.query(
    `SELECT id, room_id, name, type, protocol, state, metadata
     FROM devices
     WHERE room_id = $1
     ORDER BY id`,
    [roomId]
  );

  return {
    room: roomResult.rows[0] || null,
    devices: deviceResult.rows,
  };
}

async function updateDeviceState(roomId, deviceId, state) {
  const result = await pool.query(
    `UPDATE devices
     SET state = $3,
         command_status = CASE
           WHEN requested_state IS NOT NULL AND $3 = requested_state THEN 'confirmed'
           ELSE command_status
         END,
         requested_state = CASE
           WHEN requested_state IS NOT NULL AND $3 = requested_state THEN NULL
           ELSE requested_state
         END,
         state_confirmed_at = CASE
           WHEN requested_state IS NOT NULL AND $3 = requested_state THEN NOW()
           ELSE state_confirmed_at
         END,
         updated_at = NOW()
     WHERE room_id = $1 AND id = $2
     RETURNING id, room_id, name, type, protocol, state, metadata`,
    [roomId, deviceId, state]
  );
  return result.rows[0] || null;
}

async function markDeviceCommandRequested(roomId, deviceId, requestedState) {
  await pool.query(
    `UPDATE devices
     SET requested_state = $3,
         command_status = 'requested',
         command_requested_at = NOW(),
         updated_at = NOW()
     WHERE room_id = $1 AND id = $2`,
    [roomId, deviceId, requestedState]
  );
}

async function markDeviceCommandPublishFailed(roomId, deviceId, requestedState) {
  await pool.query(
    `UPDATE devices
     SET requested_state = $3,
         command_status = 'publish_failed',
         command_requested_at = NOW(),
         updated_at = NOW()
     WHERE room_id = $1 AND id = $2`,
    [roomId, deviceId, requestedState]
  );
}

async function updateDeviceStateFromPayload(event) {
  if (!isMqttRuntimeEvent(event)) return;

  const payload = event.payload || {};
  const updates = Array.isArray(payload.devices) ? payload.devices : [payload];

  for (const item of updates) {
    if (!item || typeof item !== 'object') continue;

    const deviceId = payloadValue(item, ['device_id', 'deviceId', 'id']);
    const state = normalizeDeviceState(payloadValue(item, ['state', 'status', 'value']));
    if (!deviceId || !state) continue;

    await updateDeviceState(event.roomId, String(deviceId), state);
  }
}

async function recordEvent(event) {
  await pool.query(
    `INSERT INTO events (room_id, category, type, topic, payload)
     VALUES ($1, $2, $3, $4, $5)`,
    [event.roomId, event.category, event.type, event.topic, event.payload]
  );
  await updateDeviceStateFromPayload(event);
}

async function listEvents({ roomId, category, type, limit } = {}) {
  const parsedLimit = Number.parseInt(limit, 10);
  const safeLimit = Number.isFinite(parsedLimit)
    ? Math.min(Math.max(parsedLimit, 1), 200)
    : 30;

  const params = [];
  const clauses = [];
  if (roomId) {
    params.push(roomId);
    clauses.push(`room_id = $${params.length}`);
  }
  if (category) {
    params.push(category);
    clauses.push(`category = $${params.length}`);
  }
  if (type) {
    params.push(type);
    clauses.push(`type = $${params.length}`);
  }
  params.push(safeLimit);

  const result = await pool.query(
    `SELECT id, room_id, category, type, topic, payload, created_at
     FROM events
     ${clauses.length ? `WHERE ${clauses.join(' AND ')}` : ''}
     ORDER BY created_at DESC
     LIMIT $${params.length}`,
    params
  );
  return result.rows;
}

module.exports = {
  pool,
  ensureSchema,
  dbHealth,
  listRooms,
  listOverview,
  listEvents,
  listLecturers,
  upsertLecturer,
  deleteLecturer,
  getRoomConfig,
  updateDeviceState,
  recordEvent,
  markDeviceCommandRequested,
  markDeviceCommandPublishFailed,
};
