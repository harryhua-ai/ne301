import { useEffect, useState } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Label } from '@/components/ui/label';
import { Button } from '@/components/ui/button';
import { useIsMobile } from '@/hooks/use-mobile';
import SvgIcon from '@/components/svg-icon';
import ImportFirmware from './fireware/import-firmware';
import ExportFirmware from './fireware/export-firmware';
import systemApis from '@/services/api/system';
import { Skeleton } from '@/components/ui/skeleton';
import { Separator } from '@/components/ui/separator';

export default function FirmwareUpgrade({ setCurrentPage }: { setCurrentPage: (page: string | null) => void }) {
    const isMobile = useIsMobile();
    const { i18n } = useLingui();
    const { getVersionsReq } = systemApis;
    const [isLoading, setIsLoading] = useState(false);
    const [versions, setVersions] = useState<{ app: string, web: string, model: string, fsbl: string, wakecore: string }>({ app: '', web: '', model: '', fsbl: '', wakecore: '' });
    const goBack = () => {
        setCurrentPage(null);
    }
    const getVersions = async () => {
        try {
            setIsLoading(true);
            const result = await getVersionsReq();
            const data = result.data as { app: string, web: string, model: string, fsbl: string, wakecore: string };
            setVersions({
                app: data.app,
                web: data.web,
                model: data.model,
                fsbl: data.fsbl,
                wakecore: data.wakecore,
            });
        } catch (error) {
            console.error('getVersions', error);
            throw error;
        } finally {
            setIsLoading(false);
        }
    }
    useEffect(() => {
        getVersions();
    }, []);

    const skeleton = () => (
        <div className="flex flex-col gap-2 p-4 rounded-lg">
            <Skeleton className="w-full h-10" />
            <Skeleton className="w-full h-10" />
            <Skeleton className="w-full h-10" />
            <Skeleton className="w-full h-10" />
            <Skeleton className="w-full h-10" />
        </div>
    )

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
            {isLoading ? skeleton() : (
                <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg">
                    <div className="flex justify-between mb-2">
                        <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.web_version')}</Label>
                        <p className="text-sm text-text-primary">{versions.web}</p>
                    </div>
                    <Separator />
                    <div className="flex justify-between mb-2">
                        <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.app_version')}</Label>
                        <p className="text-sm text-text-primary">{versions.app}</p>
                    </div>
                    <Separator />
                    <div className="flex justify-between mb-2">
                        <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.ai_model_version')}</Label>
                        <p className="text-sm text-text-primary">{versions.model}</p>
                    </div>
                    <Separator />
                    <div className="flex justify-between mb-2">
                        <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.fsbl_version')}</Label>
                        <p className="text-sm text-text-primary">{versions.fsbl}</p>
                    </div>
                    <Separator />
                    <div className="flex justify-between">
                        <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.wakeCore_version')}</Label>
                        <p className="text-sm text-text-primary">{versions.wakecore}</p>
                    </div>
                </div>
            )}
            <div className="flex gap-2 justify-end">
                <Button variant="outline" className="mt-4" onClick={() => setIsImportFirmwareDialogOpen(true)}>{i18n._('sys.system_management.header_import_firmware')}</Button>
                <Button variant="primary" className="mt-4" onClick={() => setIsExportFirmwareDialogOpen(true)}>{i18n._('sys.system_management.export_firmware')}</Button>
            </div>
            <ImportFirmware isImportFirmwareDialogOpen={isImportFirmwareDialogOpen} setIsImportFirmwareDialogOpen={setIsImportFirmwareDialogOpen} />
            <ExportFirmware isExportFirmwareDialogOpen={isExportFirmwareDialogOpen} setIsExportFirmwareDialogOpen={setIsExportFirmwareDialogOpen} />
        </div>
    )
}