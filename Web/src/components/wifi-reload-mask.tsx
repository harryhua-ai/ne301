import { useState, useEffect } from 'preact/hooks';
import { createPortal } from 'preact/compat';
import { useLingui } from '@lingui/react';
import Loading from "@/components/loading";
import SvgIcon from '@/components/svg-icon';
/** 
 * @TODO
* - loading10s: If connection succeeds, it means reconnected, close mask
* - If interface is still not accessible after 10s, prompt user to reconnect network 
 */

export default function WifiReloadMask({ loadingText, isLoading = false, maskText = '' }: {loadingText?: string, isLoading?: boolean, maskText?: string }) {
    const [showReConnectWifi, setShowReConnectWifi] = useState(false);
    const { i18n } = useLingui();
    useEffect(() => {
        if (!isLoading) {
            setShowReConnectWifi(true);
        }
    }, [isLoading]);
    return createPortal(
        <div className="fixed inset-0 z-50 flex items-center justify-center">
            {/* Background mask */}
            <div className="fixed inset-0 bg-black/80 backdrop-blur-sm" />
            
            {/* Content area: increase z-index to avoid being blocked by mask */}
            <div className="relative z-[51] flex flex-col items-center justify-center gap-4 p-6">
                {showReConnectWifi ? (
                    <>
                        <SvgIcon icon="wifi_off" className="w-20 h-20 text-gray-200" />
                        <div className="text-center">
                            <p className="text-lg font-medium text-white">  
                                {maskText || i18n._('sys.system_management.network_disconnected')}
                            </p>
                        </div>
                    </>
                ) : (
                    <Loading placeholder={loadingText} />
                )}
            </div>
        </div>,
        document.body
    )
}