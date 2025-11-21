import { useState } from 'preact/hooks';
import { Outlet } from 'react-router-dom';

import Menu from './pc/menu';
import SvgIcon from '@/components/svg-icon';
import ActionButtons from './pc/action-buttons';
import { Button } from '@/components/ui/button';

import { useAuthStore } from '@/store/auth';
import NavRight from './mobile/nav-right';
import { Link } from 'react-router-dom';
import DeviceInfo from './pc/deviceInfo';
import { useIsMobile } from '@/hooks/use-mobile';
import Log from './log';

export default function Layout() {
  const { isValidateToken } = useAuthStore();
  const [showMenu, setShowMenu] = useState(false);
  const isMobile = useIsMobile()
  const [isOpen, setIsOpen] = useState(false)
  const handleOpenLogs = () => {
    setShowMenu(false)
    setIsOpen(true)
  }
  return (
    <div className="h-screen w-screen bg-gray-100 relative flex flex-col">
      <header
        className={`${isValidateToken ? 'bg-white ' : 'bg-gray-100'}  relative z-11`}
      >
        <div className="w-full mx-auto px-4 sm:px-6 lg:px-8">
          <div className="flex justify-between h-16">
            {/* Left: Logo and device information */}
            <div className="flex items-center space-x-6">
              {/* Cam Think Logo */}
              <div className="flex items-center">
                <div className="relative h-full">
                  <Link to="/device-tool">
                    <SvgIcon className="w-25 h-14" icon="CamthinkLogo" />
                  </Link>
                </div>
              </div>
              {isValidateToken && (
                <div className="w-[1px] h-8 bg-gray-200"></div>
              )}
              {/* Device information */}
              {isValidateToken && <DeviceInfo />}
            </div>
            {/* Right: function buttons */}
            <div className="flex items-center space-x-3">
              {/* Import button - hidden on mobile */}
              {!isMobile && (
                <div className="md:flex items-center space-x-2">
                  <ActionButtons />
                </div>
              )}

              {isMobile
                && (
                  <div>
                    <Button
                      variant="ghost"
                      size="icon"
                      onClick={() => setShowMenu(true)}
                    >
                      <SvgIcon className="!w-8 !h-8" icon="menu" />
                    </Button>
                    {showMenu && <NavRight onClose={() => setShowMenu(false)} handleOpenLogs={handleOpenLogs} />}
                    <Log isOpen={isOpen} setOpen={setIsOpen} />
                  </div>
                )}
            </div>
          </div>
        </div>
      </header>

      {/* Navigation menu */}
      {isValidateToken && <Menu />}
      <main className="flex-1 overflow-auto w-full relative h-full">
        <Outlet />
      </main>
    </div>
  );
}
