import { create } from 'zustand'
import systemApis, { type DeviceInfo } from '@/services/api/system'

type SetTimePayload = {
  timestamp: number
  timezone: string
}

type SetTimeState = {
  loading: boolean
  error: string | null
  deviceInfo: DeviceInfo | null
  setSystemTime: (payload: SetTimePayload) => Promise<boolean>
  getDeviceInfo: () => Promise<DeviceInfo | null>
}

export const useSystemInfo = create<SetTimeState>()((set, _get) => ({
  loading: false,
  error: null,
  deviceInfo: null,
  async setSystemTime(payload) {
    set({ loading: true, error: null })
    try {
      await systemApis.setSystemTimeReq(payload)
      return true
    } catch (e: any) {
      set({ error: e?.message ?? 'Failed to set system time' })
      return false
    } finally {
      set({ loading: false })
    }
  },
  async getDeviceInfo() {
    set({ loading: true, error: null })
    try {
      const res = await systemApis.getDeviceInfoReq()
      const data = res.data as DeviceInfo
      set({ deviceInfo: data })
      return data
    } catch (e: any) {
      set({ error: e?.message ?? 'Failed to get device information' })
      return null
    } finally {
      set({ loading: false })
    }
  },
}))