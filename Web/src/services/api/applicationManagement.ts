import request from '../request';

type QosLevel = 0 | 1 | 2;

interface MqttAuthentication {
  username: string;
  password: string;
  ca_cert_path: string;
  client_cert_path: string;
  client_key_path: string;
  ca_data?: string;
  client_cert_data?: string;
  client_key_data?: string;
  sni?: boolean;
}

type ProtocolType = 'mqtt' | 'mqtts';

interface MqttConnection {
  hostname: string;
  port: number;
  client_id: string;
  protocol_type: ProtocolType;
  sni?: boolean;
}

interface MqttQos {
  data_receive_qos: QosLevel;
  data_report_qos: QosLevel;
}

interface MqttTopics {
  data_receive_topic: string;
  data_report_topic: string;
}

interface MqttStatus {
  connected: boolean;
  running: boolean;
  state: number;
  version: string;
}

export interface SetMqttConfigReq {
  authentication: MqttAuthentication;
  connection: MqttConnection;
  qos: MqttQos;
  topics: MqttTopics;
  status: MqttStatus;
}

const applicationManagement = {
  getMqttConfigReq: () => request.get('/api/v1/apps/mqtt/config'),
  setMqttConfigReq: (data: SetMqttConfigReq) => request.post('/api/v1/apps/mqtt/config', data),
  connectMqttReq: () => request.post('/api/v1/apps/mqtt/connect'),
  disconnectMqttReq: () => request.post('/api/v1/apps/mqtt/disconnect'),
};

export default applicationManagement;
