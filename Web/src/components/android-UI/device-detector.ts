// Android device detection utility
export const DeviceDetector = {
  // Detect if device is Android
  isAndroid(): boolean {
    if (typeof window === 'undefined') return false;
    return /Android/i.test(navigator.userAgent);
  },

  // Detect if device is mobile
  isMobile(): boolean {
    if (typeof window === 'undefined') return false;
    return /Android|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent);
  },

  // Detect if device is touch device
  isTouchDevice(): boolean {
    if (typeof window === 'undefined') return false;
    return 'ontouchstart' in window || navigator.maxTouchPoints > 0;
  },

  // Get device information
  getDeviceInfo() {
    if (typeof window === 'undefined') {
      return {
        userAgent: 'SSR',
        isAndroid: false,
        isMobile: false,
        isTouch: false,
        platform: 'unknown'
      };
    }

    return {
      userAgent: navigator.userAgent,
      isAndroid: this.isAndroid(),
      isMobile: this.isMobile(),
      isTouch: this.isTouchDevice(),
      platform: navigator.platform
    };
  },

  // Debug information
  debug() {
    const info = this.getDeviceInfo();
    return info;
  }
};

// Export default detection functions
export const { isAndroid, isMobile, isTouchDevice } = DeviceDetector;
