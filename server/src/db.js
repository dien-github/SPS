const { Pool } = require('pg');

const pool = new Pool({
  host: process.env.DB_HOST || 'localhost',
  port: Number(process.env.DB_PORT || 5432),
  user: process.env.DB_USER || 'sps',
  password: process.env.DB_PASSWORD || 'sps_secret',
  database: process.env.DB_NAME || 'sps_db',
});

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
      updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
    );

    CREATE TABLE IF NOT EXISTS devices (
      id TEXT PRIMARY KEY,
      room_id TEXT NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
      name TEXT NOT NULL,
      type TEXT NOT NULL,
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

  const rooms = [
    ['room101', 'Phòng 101', 'Tầng 1 - Khu A'],
    ['room102', 'Phòng 102', 'Tầng 1 - Khu A'],
    ['lab201', 'Phòng Lab 201', 'Tầng 2 - Khu B'],
  ];

  for (const room of rooms) {
    await pool.query(
      `INSERT INTO rooms (id, name, location)
       VALUES ($1, $2, $3)
       ON CONFLICT (id)
       DO UPDATE SET name = EXCLUDED.name,
                     location = EXCLUDED.location,
                     updated_at = NOW()`,
      room
    );
  }

  const devices = [
    ['room101-device-a', 'room101', 'Thiết bị A - Đèn bục giảng', 'relay', 'off', { label: 'A', channel: 1 }],
    ['room101-device-b', 'room101', 'Thiết bị B - Máy chiếu', 'projector', 'off', { label: 'B', channel: 2 }],
    ['room101-device-c', 'room101', 'Thiết bị C - Điều hòa', 'ac', 'off', { label: 'C', channel: 3 }],
    ['room102-device-a', 'room102', 'Thiết bị A - Đèn lớp', 'relay', 'off', { label: 'A', channel: 1 }],
    ['room102-device-b', 'room102', 'Thiết bị B - Màn chiếu', 'projector', 'off', { label: 'B', channel: 2 }],
    ['room102-device-c', 'room102', 'Thiết bị C - Âm thanh', 'relay', 'off', { label: 'C', channel: 3 }],
    ['lab201-device-a', 'lab201', 'Thiết bị A - Đèn lab', 'relay', 'off', { label: 'A', channel: 1 }],
    ['lab201-device-b', 'lab201', 'Thiết bị B - Máy chiếu lab', 'projector', 'off', { label: 'B', channel: 2 }],
    ['lab201-device-c', 'lab201', 'Thiết bị C - Điều hòa lab', 'ac', 'off', { label: 'C', channel: 3 }],
  ];

  for (const device of devices) {
    await pool.query(
      `INSERT INTO devices (id, room_id, name, type, state, metadata)
       VALUES ($1, $2, $3, $4, $5, $6)
       ON CONFLICT (id)
       DO UPDATE SET name = EXCLUDED.name,
                     type = EXCLUDED.type,
                     metadata = EXCLUDED.metadata,
                     updated_at = NOW()`,
      device
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
            COUNT(d.id)::int AS device_count,
            COUNT(*) FILTER (WHERE d.state = 'on')::int AS active_device_count
     FROM rooms r
     LEFT JOIN devices d ON d.room_id = r.id
     GROUP BY r.id
     ORDER BY r.id`
  );
  return result.rows;
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
    'SELECT id, name, location FROM rooms WHERE id = $1',
    [roomId]
  );
  const deviceResult = await pool.query(
    'SELECT id, name, type, state, metadata FROM devices WHERE room_id = $1 ORDER BY id',
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
         updated_at = NOW()
     WHERE room_id = $1 AND id = $2
     RETURNING id, room_id, name, type, state, metadata`,
    [roomId, deviceId, state]
  );
  return result.rows[0] || null;
}

async function recordEvent(event) {
  await pool.query(
    `INSERT INTO events (room_id, category, type, topic, payload)
     VALUES ($1, $2, $3, $4, $5)`,
    [event.roomId, event.category, event.type, event.topic, event.payload]
  );
}

module.exports = {
  pool,
  ensureSchema,
  dbHealth,
  listRooms,
  listLecturers,
  upsertLecturer,
  deleteLecturer,
  getRoomConfig,
  updateDeviceState,
  recordEvent,
};
