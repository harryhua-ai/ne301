import request from '../request';

interface SetWifiReq {
    interface: string;
    ssid: string;
    password: string;
    ap_sleep_time?: number;
    bssid: string;
}
interface SetWifiConfigReq {
    interface: string;
    ssid: string;
    password: string;
    ap_sleep_time: number;
}
interface DeleteWifiReq {
    ssid: string;
    bssid: string;
}

const systemSettings = {
    getNetworkStatus: () => request.get('/api/v1/system/network/status'),
    scanWifi: () => request.post('/api/v1/system/network/scan'),
    setWifi: (data: SetWifiReq) => request.post('/api/v1/system/network/wifi', data),
    setWifiConfig: (data: SetWifiConfigReq) => request.post('/api/v1/system/network/wifi', data),
    deleteWifi: (data: DeleteWifiReq) => request.post('/api/v1/system/network/delete', data),
    disconnectWifi: (data: { interface: string }) => request.post('/api/v1/system/network/disconnect', data),
    restartDevice: ({ delaySeconds }: { delaySeconds: number }) => request.post('/api/v1/system/restart', { delay_seconds: delaySeconds }),

}

export default systemSettings;