import { useEffect } from 'preact/hooks';
import router from '@/router';
import { RouterProvider } from 'react-router-dom';
import { setItem, hasItem } from '@/utils/storage';
import { Toaster } from 'sonner';
import systemApis from '@/services/api/system'
import { useSystemInfo } from '@/store/systemInfo'

export default function App() {
  // Set system time
  const { setSystemTimeReq } = systemApis
  const { getDeviceInfo } = useSystemInfo()

  const timingTask = () => {
    getDeviceInfo()
  const timer =  setInterval(() => {
      getDeviceInfo();
    }, 1000 * 60 * 10);
    return timer;
  }
  const setSystemTime = () => {
    try {
      // Get current UTC time in seconds timestamp
      const timestamp = Math.floor(Date.now() / 1000)
      const offsetMinutes = new Date().getTimezoneOffset()
      const timezone = `UTC${offsetMinutes <= 0 ? '+' : '-'}${Math.abs(offsetMinutes) / 60}`
      const parameter = {
        timestamp,
        timezone
      }
      setSystemTimeReq(parameter)
    } catch (error) {
      console.error(error)
    }
  }
  const setGuideFlag = () => {
    const { pathname } = window.location;
    if (pathname.includes('device-tool')) {
      if (!hasItem('guideFlag')) {
        setItem('guideFlag', 'true');
      }
    }
  };
  
  useEffect(() => {
    setGuideFlag();
    setSystemTime();
    const timer = timingTask();
    return () => {
      clearInterval(timer);
    }
  }, []);

  return (
    <>
      <RouterProvider router={router} />
      <Toaster
        position="top-center"
        richColors
        duration={4000}
      />
    </>
  );
}