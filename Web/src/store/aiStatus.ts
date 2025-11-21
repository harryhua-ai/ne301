import { create } from 'zustand';

interface AiStatusState {
  isAiInference: boolean;
}

interface AiStatusActions {
  setIsAiInference: (isAiInference: boolean) => void;
}

export const useAiStatusStore = create<AiStatusState & AiStatusActions>((set) => ({
  isAiInference: false,
  setIsAiInference: (isAiInference: boolean) => set({ isAiInference }),
}));