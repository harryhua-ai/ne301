import { create } from 'zustand'
import systemSettings from '@/services/api/systemSettings'

interface CommunicationDataState {
    error: string | null
    communicationData: any
    setCommunicationData: () => Promise<any>
}

export const useCommunicationData = create<CommunicationDataState>((set) => ({
    error: null,
    communicationData: null,
    async setCommunicationData() {
        set({ error: null })
        try {
            const res = await systemSettings.getNetworkStatus()
            set({ communicationData: res.data })
            return res
        } catch (error: any) {
            set({ error: error?.message ?? 'Failed to get communication data' })
        }
    }
}))