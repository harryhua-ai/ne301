import { useState } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Label } from '@/components/ui/label';
import { Button } from '@/components/ui/button';
import { useIsMobile } from '@/hooks/use-mobile';
import SvgIcon from '@/components/svg-icon';
import ImportFirmware from './fireware/import-firmware';
import ExportFirmware from './fireware/export-firmware';
import { useSystemInfo } from '@/store/systemInfo'

export default function FirmwareUpgrade({ setCurrentPage }: { setCurrentPage: (page: string | null) => void }) {
    const isMobile = useIsMobile();
    const { i18n } = useLingui();
    const { deviceInfo } = useSystemInfo();
    const goBack = () => {
        setCurrentPage(null);
    }
    const [isImportFirmwareDialogOpen, setIsImportFirmwareDialogOpen] = useState(false);
    const [isExportFirmwareDialogOpen, setIsExportFirmwareDialogOpen] = useState(false);
    const renderBackSlot = () => {
        if (isMobile) {
            return (
                <div className="flex text-lg font-bold justify-start items-center gap-2">
                    <div onClick={() => goBack()} className="cursor-pointer">
                        <SvgIcon icon="arrow_left" className="w-6 h-6" />
                    </div>
                    <p>{i18n._('sys.system_management.firmware_upgrade')}</p>
                </div>
            )
        }
    }
    return (
        <div>
            {isMobile && renderBackSlot()}
            <p className="text-sm text-text-primary font-bold mt-5 mb-2">{i18n._('sys.system_management.firmware_info')}</p>
            <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg">
                <div className="flex justify-between">
                    <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.firmware_version')}</Label>
                    <Label className="text-sm text-text-primary">{deviceInfo?.software_version}</Label>
                </div>
            </div>
            <div className="flex gap-2 justify-end">
                <Button variant="outline" className="mt-4" onClick={() => setIsImportFirmwareDialogOpen(true)}>{i18n._('sys.system_management.header_import_firmware')}</Button>
                <Button variant="primary" className="mt-4" onClick={() => setIsExportFirmwareDialogOpen(true)}>{i18n._('sys.system_management.export_firmware')}</Button>
            </div>
            <ImportFirmware isImportFirmwareDialogOpen={isImportFirmwareDialogOpen} setIsImportFirmwareDialogOpen={setIsImportFirmwareDialogOpen} />
            <ExportFirmware isExportFirmwareDialogOpen={isExportFirmwareDialogOpen} setIsExportFirmwareDialogOpen={setIsExportFirmwareDialogOpen} />
        </div>
    )
}