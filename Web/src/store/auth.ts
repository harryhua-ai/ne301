import { create } from 'zustand';
import { getItem, setItem, removeItem } from '@/utils/storage';

interface AuthState {
  isValidateToken: boolean;
  loading: boolean;
}

interface AuthActions {
  initValidateToken: () => boolean;
  setToken: (token: string) => void;
  clearToken: () => void;
}

type AuthStore = AuthState & AuthActions;

const enableAuth = import.meta.env.VITE_ENABLE_AUTH === 'true';
export const useAuthStore = create<AuthStore>((set, _get) => ({
  isValidateToken: false,
  loading: true,

  initValidateToken: () => {
    const token = getItem('token');
    const currentTime = Date.now();
    const lastRequestTime = Number(getItem('lastRequestTime')) || 0;
    const timeout = Number(import.meta.env.VITE_LOGIN_TIMEOUT) || 3600000;

    if (enableAuth) {
      const isValid = Boolean(token) && (currentTime - lastRequestTime) < (timeout * 1000);
      set({ isValidateToken: isValid, loading: false });    
      return isValid;
    }
    const isValid = Boolean(token)
      set({ isValidateToken: isValid, loading: false });
      return isValid;
  },

  setToken: (token: string) => {
    setItem('token', token);
    setItem('lastRequestTime', Date.now());
    set({ isValidateToken: true });
  },

  clearToken: () => {
    removeItem('token');
    removeItem('lastRequestTime');
    set({ isValidateToken: false });
  },
}));

// Export store instance for accessing outside components
export const authStore = useAuthStore.getState; 