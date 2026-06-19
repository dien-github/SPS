const express = require('express');
const multer = require('multer');
const path = require('path');
const fs = require('fs');

const app = express();
const PORT = process.env.PORT || 3000;
const LOGIN_PASSWORD = '123456';

const ROOT = __dirname;
const PUBLIC_DIR = path.join(ROOT, 'public');
const DATA_DIR = path.join(ROOT, 'data');
const UPLOAD_DIR = path.join(ROOT, 'uploads');
const LECTURERS_FILE = path.join(DATA_DIR, 'lecturers.json');
const FIRMWARE_FILE = path.join(DATA_DIR, 'firmware.json');

function ensureDir(dir) {
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
}

function ensureJsonFile(file, defaultValue) {
  if (!fs.existsSync(file)) {
    fs.writeFileSync(file, JSON.stringify(defaultValue, null, 2), 'utf8');
  }
}

function readJson(file, fallback) {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch {
    return fallback;
  }
}

function writeJson(file, data) {
  fs.writeFileSync(file, JSON.stringify(data, null, 2), 'utf8');
}

ensureDir(PUBLIC_DIR);
ensureDir(DATA_DIR);
ensureDir(UPLOAD_DIR);

ensureJsonFile(LECTURERS_FILE, [
  { id: 1, name: 'Nguyễn Văn A', code: 'GV001' },
  { id: 2, name: 'Trần Thị B', code: 'GV002' }
]);

ensureJsonFile(FIRMWARE_FILE, {
  lastUploadedFile: null,
  originalName: null,
  updatedAt: null,
  size: null
});

const devices = [
  { id: 'may-lanh', name: 'Máy lạnh', status: false },
  { id: 'may-chieu', name: 'Máy chiếu', status: false },
  { id: 'man-hinh', name: 'Màn hình', status: false },
  { id: 'am-thanh', name: 'Âm thanh', status: false }
];

const storage = multer.diskStorage({
  destination: (req, file, cb) => cb(null, UPLOAD_DIR),
  filename: (req, file, cb) => cb(null, `${Date.now()}-${file.originalname.replace(/\s+/g, '-')}`)
});
const upload = multer({ storage });

app.use(express.json());
app.use(express.urlencoded({ extended: true }));
app.use('/uploads', express.static(UPLOAD_DIR));
app.use(express.static(PUBLIC_DIR));

app.post('/api/login', (req, res) => {
  const { password } = req.body || {};
  if (password !== LOGIN_PASSWORD) {
    return res.status(401).json({ success: false, message: 'Sai mật khẩu đăng nhập.' });
  }
  return res.json({ success: true, message: 'Đăng nhập thành công.' });
});

app.get('/api/devices', (req, res) => {
  res.json(devices);
});

app.post('/api/devices/:id/control', (req, res) => {
  const device = devices.find(d => d.id === req.params.id);
  if (!device) {
    return res.status(404).json({ success: false, message: 'Không tìm thấy thiết bị.' });
  }
  device.status = !!req.body.status;
  res.json({ success: true, message: 'Đã cập nhật trạng thái thiết bị.', device });
});

app.get('/api/firmware/info', (req, res) => {
  res.json(readJson(FIRMWARE_FILE, {}));
});

app.post('/api/firmware/upload', upload.single('firmwareFile'), (req, res) => {
  if (!req.file) {
    return res.status(400).json({ success: false, message: 'Bạn chưa chọn file firmware.' });
  }

  const firmwareInfo = {
    lastUploadedFile: req.file.filename,
    originalName: req.file.originalname,
    updatedAt: new Date().toLocaleString('vi-VN'),
    size: req.file.size
  };

  writeJson(FIRMWARE_FILE, firmwareInfo);
  res.json({ success: true, message: 'Tải firmware thành công.', data: firmwareInfo });
});

app.get('/api/lecturers', (req, res) => {
  res.json(readJson(LECTURERS_FILE, []));
});

app.post('/api/lecturers', (req, res) => {
  const lecturers = readJson(LECTURERS_FILE, []);
  const name = (req.body.name || '').trim();
  const code = (req.body.code || '').trim().toUpperCase();

  if (!name || !code) {
    return res.status(400).json({ success: false, message: 'Vui lòng nhập đầy đủ tên và mã giảng viên.' });
  }

  if (lecturers.some(x => x.code.toUpperCase() === code)) {
    return res.status(400).json({ success: false, message: 'Mã giảng viên đã tồn tại.' });
  }

  const newLecturer = {
    id: lecturers.length ? Math.max(...lecturers.map(x => x.id)) + 1 : 1,
    name,
    code
  };

  lecturers.push(newLecturer);
  writeJson(LECTURERS_FILE, lecturers);
  res.status(201).json({ success: true, message: 'Đã thêm giảng viên.', lecturer: newLecturer });
});

app.put('/api/lecturers/:id', (req, res) => {
  const lecturers = readJson(LECTURERS_FILE, []);
  const id = Number(req.params.id);
  const index = lecturers.findIndex(x => x.id === id);

  if (index === -1) {
    return res.status(404).json({ success: false, message: 'Không tìm thấy giảng viên.' });
  }

  const name = (req.body.name || '').trim();
  const code = (req.body.code || '').trim().toUpperCase();

  if (!name || !code) {
    return res.status(400).json({ success: false, message: 'Vui lòng nhập đầy đủ tên và mã giảng viên.' });
  }

  if (lecturers.some(x => x.id !== id && x.code.toUpperCase() === code)) {
    return res.status(400).json({ success: false, message: 'Mã giảng viên đã tồn tại.' });
  }

  lecturers[index] = { ...lecturers[index], name, code };
  writeJson(LECTURERS_FILE, lecturers);
  res.json({ success: true, message: 'Đã cập nhật giảng viên.', lecturer: lecturers[index] });
});

app.delete('/api/lecturers/:id', (req, res) => {
  const lecturers = readJson(LECTURERS_FILE, []);
  const id = Number(req.params.id);
  const filtered = lecturers.filter(x => x.id !== id);

  if (filtered.length === lecturers.length) {
    return res.status(404).json({ success: false, message: 'Không tìm thấy giảng viên.' });
  }

  writeJson(LECTURERS_FILE, filtered);
  res.json({ success: true, message: 'Đã xóa giảng viên.' });
});

app.get('/api/sync-suggestion', (req, res) => {
  res.json({
    methods: [
      'Thiết bị trung tâm gọi GET /api/lecturers để lấy danh sách mới nhất.',
      'Khi cập nhật danh sách giảng viên, server có thể trả dữ liệu mới ngay cho dashboard.',
      'ESP32 có thể đồng bộ khi khởi động hoặc khi người dùng mở màn hình chọn giảng viên.'
    ]
  });
});

app.get('*', (req, res) => res.sendFile(path.join(PUBLIC_DIR, 'index.html')));

app.listen(PORT, () => console.log(`Server đang chạy tại http://localhost:${PORT}`));