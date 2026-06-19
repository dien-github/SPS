const mqtt = require('mqtt');

function parseTopic(topic) {
  const parts = topic.split('/');
  if (parts.length < 4 || parts[0] !== 'sps') {
    return null;
  }

  return {
    roomId: parts[1],
    category: parts[2],
    type: parts.slice(3).join('/'),
  };
}

function parsePayload(message) {
  const text = message.toString('utf8');
  try {
    return JSON.parse(text);
  } catch {
    return { raw: text };
  }
}

function createMqttWorker({ onEvent } = {}) {
  const host = process.env.MQTT_HOST || 'localhost';
  const port = Number(process.env.MQTT_PORT || 1883);
  const url = `mqtt://${host}:${port}`;
  let client = null;
  let connected = false;

  function start() {
    client = mqtt.connect(url, {
      clientId: `sps-api-${process.pid}`,
      reconnectPeriod: 3000,
    });

    client.on('connect', () => {
      connected = true;
      console.log(`MQTT connected: ${url}`);
      client.subscribe(['sps/+/event/#', 'sps/+/status/#'], { qos: 1 }, (err) => {
        if (err) {
          console.error('MQTT subscribe failed:', err.message);
        }
      });
    });

    client.on('close', () => {
      connected = false;
    });

    client.on('error', (err) => {
      console.error('MQTT error:', err.message);
    });

    client.on('message', async (topic, message) => {
      const parsed = parseTopic(topic);
      if (!parsed || !onEvent) {
        return;
      }

      try {
        await onEvent({
          ...parsed,
          topic,
          payload: parsePayload(message),
        });
      } catch (err) {
        console.error('Failed to record MQTT event:', err.message);
      }
    });
  }

  function publishCommand(roomId, commandType, payload) {
    if (!client || !connected) {
      return Promise.resolve(false);
    }

    const topic = `sps/${roomId}/cmd/${commandType}`;
    const body = Buffer.from(JSON.stringify(payload));

    return new Promise((resolve) => {
      client.publish(topic, body, { qos: 1, retain: false }, (err) => {
        if (err) {
          console.error(`MQTT publish failed for ${topic}:`, err.message);
          resolve(false);
          return;
        }
        resolve(true);
      });
    });
  }

  function getStatus() {
    return {
      url,
      connected,
    };
  }

  return {
    start,
    publishCommand,
    getStatus,
  };
}

module.exports = {
  createMqttWorker,
};
