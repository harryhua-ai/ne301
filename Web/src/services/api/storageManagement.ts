import request from "../request";

const storageManagement = {
    getStorage: () => request.get('/api/v1/device/storage')
} 

export default storageManagement