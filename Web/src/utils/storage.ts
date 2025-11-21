// Error type
interface StorageError {
  code: 'QUOTA_EXCEEDED' | 'NOT_SUPPORTED' | 'ACCESS_DENIED' | 'UNKNOWN';
  message: string;
  key?: string;
}

// Check if localStorage is available
const isStorageAvailable = (): boolean => {
  try {
    if (typeof window === 'undefined' || !window.localStorage) {
      return false;
    }
    const test = '__storage_test__';
    localStorage.setItem(test, test);
    localStorage.removeItem(test);
    return true;
  } catch {
    return false;
  }
};

// Basic getItem
export const getItem = <T = any>(key: string): T | null => {
  try {
    if (!isStorageAvailable()) {
      throw new Error('localStorage is not available');
    }

    const item = localStorage.getItem(key);
    if (item === null) {
      return null;
    }

    return JSON.parse(item);
  } catch (err) {
    console.error('Failed to get item:', err);
    return null;
  }
};

// Basic setItem
export const setItem = <T = any>(key: string, value: T): void => {
  try {
    if (!isStorageAvailable()) {
      throw new Error('localStorage is not available');
    }

    localStorage.setItem(key, JSON.stringify(value));
  } catch (err) {
    console.error('Failed to set item:', err);
  }
};

// Remove specified item
export const removeItem = (key: string): void => {
  try {
    if (!isStorageAvailable()) {
      throw new Error('localStorage is not available');
    }

    localStorage.removeItem(key);
  } catch (err) {
    console.error('Failed to remove item:', err);
  }
};

// Clear all items
export const clear = (): void => {
  try {
    if (!isStorageAvailable()) {
      throw new Error('localStorage is not available');
    }

    localStorage.clear();
  } catch (err) {
    console.error('Failed to clear storage:', err);
  }
};

// Check if specified item exists
export const hasItem = (key: string): boolean => getItem(key) !== null;

// Export type
export type { StorageError }; 