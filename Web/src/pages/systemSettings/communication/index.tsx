import { useState } from 'react';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';
import WifiNetworkPage from './wifi-network';
import CellularNetworkPage from './cellular-network';
import PoeNetworkPage from './poe';
import { useIsMobile } from '@/hooks/use-mobile';

export default function Communication({ setCurrentPage }: { setCurrentPage: (page: string | null) => void }) {
    const { i18n } = useLingui();
    const isMobile = useIsMobile();
    const [communicationMode] = useState('wifi');
    const goBack = () => {
        setCurrentPage(null);
    }

    const renderBackSlot = () => {
        if (isMobile) {
            return (
                <div className="flex text-lg font-bold justify-start items-center gap-2">
                    <div onClick={() => goBack()} className="cursor-pointer">
                        <SvgIcon icon="arrow_left" className="w-6 h-6" />
                    </div>
                    <p>{i18n._('sys.system_management.device_password')}</p>
                </div>
            )
        }
    }
    return (
        <div>
            {isMobile && renderBackSlot()}
            {/* <div className="flex gap-2 justify-between my-4">
                <Label className="text-sm font-bold text-text-primary">{i18n._('sys.system_management.change_communication')}</Label>
                <Select value={communicationMode} onValueChange={setCommunicationMode}>
                    <SelectTrigger>
                        <SelectValue placeholder={i18n._('sys.system_management.communication_mode')} />
                    </SelectTrigger>
                    <SelectContent>
                        <SelectItem value="wifi">{i18n._('sys.system_management.wifi')}</SelectItem>
                        <SelectItem value="cellular">{i18n._('sys.system_management.cellular_network')}</SelectItem>
                        <SelectItem value="poe">{i18n._('sys.system_management.poe_network')}</SelectItem>
                    </SelectContent>
                </Select>
            </div> */}
            {communicationMode === 'wifi' && <WifiNetworkPage />}
            {communicationMode === 'cellular' && <CellularNetworkPage />}
            {communicationMode === 'poe' && <PoeNetworkPage />}
        </div>
    )
}