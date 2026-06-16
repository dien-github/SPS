# Architecture of Backend deploy on Server

## 1. Master Database
### 1.1. Relatinonal DB
- Tech: 
- Function:
  - Store Lecturers UID
  - Store Rooms: list room, device in room
### 1.2. File Storage
- Static folder to store firmware of MCU and the application static binary on SBC.
## 2. MQTT Broker
- Send/Receive event and command in JSON.
- Topic:
  - `sps/+/status/connection`: Listen LWT of each PCD.
  - `sps/+/cmd/#`: server send command remote.
  - `sps/+/status/dbsync`: server send new UID from database.
  - `sps/+/status/ota`: server send signal new update ota.
## 3. Node.js/Express.js Backend
### 3.1. RESTful API
- Provide endpoint for Web Dashboard.
### 3.2. Sync Service
- When have an update about the Lecuter database, backend publish an event to PCD for update the database at edge.
### 3.3. OTA Hybrid Server
- When have update, Backend send command through OTA (version and checksum), PCD use HTTP GET to pull the file from File Storage of Server.
## 4. MQTT-to-DB Worker
### 4.1. Server -> PCD
Admin update new RFID through API, get data from database, packet to JSON and publish to MQTT for PCD to store in local database.
### 4.2. PCD -> Server
Worker subscribe event topic from PCD and insert to DB for Web Dashboard have data to display.
