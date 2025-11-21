import { useState } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { useIsMobile } from '@/hooks/use-mobile';

export default function LocalBluetooth({ setCurrentPage }: { setCurrentPage: (page: string | null) => void }) {
    const { i18n } = useLingui();

    const [bluetoothStatus] = useState(false);
    const [pinCode] = useState('');

    // const [bluetoothList, setBluetoothList] = useState([
    //     {
    //         name: 'Bluetooth Device 1',
    //         address: '00:00:00:00:00:00',
    //         status: 'connected'
    //     },
    //     {
    //         name: 'Bluetooth Device 2',
    //         address: '00:00:00:00:00:01',
    //         status: 'disconnected'
    //     },
    //     {
    //         name: 'Bluetooth Device 3',
    //         address: '00:00:00:00:00:02',
    //         status: 'connected'
    //     }
    // ]);
    
    const isMobile = useIsMobile();
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
                    <p>{i18n._('sys.system_management.bluetooth_title')}</p>
                </div>
            )
        }
    }
    return (
        <div>
            {isMobile && renderBackSlot()}
            <div className="flex flex-col gap-2 mt-4 bg-gray-100 p-4 rounded-lg">
                <div className="flex justify-between">
                    <div className="flex items-center gap-2">
                        <div className="p-1 rounded-md bg-primary flex items-center justify-center">
                            <SvgIcon icon="bluetooth" className="w-4 h-4" />
                        </div>
                        <p className="text-sm text-text-primary font-bold">{i18n._('sys.system_management.bluetooth_status')}</p>
                    </div>
                    <div className="flex items-center gap-1">
                        <SvgIcon icon={bluetoothStatus ? 'bluetooth_connected' : 'bluetooth_disabled'} className={`w-4 h-4 ${bluetoothStatus ? 'text-green-500' : 'text-red-500'}`} />
                        <p className={`text-sm font-medium ${bluetoothStatus ? 'text-green-500' : 'text-red-500'}`}>{i18n._(`sys.system_management.${bluetoothStatus ? 'connected' : 'disconnected'}`)}</p>
                    </div>
                </div>
                <Separator />
                <div className="flex justify-between">
                    <Label className="text-sm text-text-primary">PIN Code</Label>
                    <Label className="text-sm text-text-primary">{pinCode || i18n._('sys.system_management.random_string')}</Label>
                </div>
            </div>
            <p className="text-sm text-text-primary font-bold mt-5 mb-2">{i18n._('sys.system_management.bluetooth_devices')}</p>
            <div className="flex flex-col gap-2 mt-4 bg-gray-100 p-4 rounded-lg">
                <div className="flex justify-between">
                    <Label className="text-sm text-text-primary">PIN Code</Label>
                    <div className="flex items-center gap-2">
                        <p className="text-sm text-text-primary">{i18n._('sys.system_management.bluetooth_devices')}</p>

                    </div>
                </div>
            </div>
        </div>
    )
}
