import { useState } from "preact/hooks";
import { useLingui } from "@lingui/react";
import { Skeleton } from "@/components/ui/skeleton";
import { Separator } from "@/components/ui/separator";
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Button } from '@/components/ui/button';

export default function CellularNetwork() {
    const { i18n } = useLingui();
    const [isLoading] = useState(false);

    const skeleton = () => (
        <div>
            <Skeleton className="h-6 w-25 mb-2" />
            <Skeleton className="h-10 w-full mb-2" />
            <Skeleton className="h-10 w-full mb-4" />
        </div>
    )
    return (
        <div>
            {isLoading && skeleton()}
            {!isLoading && (
                <div>
                    <p className="text-sm font-bold mb-2">{i18n._('sys.system_management.cellular_network')}</p>
                    <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg">
                        <div className="flex justify-between">
                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.username')}</Label>
                            <Input variant="ghost" placeholder={i18n._('common.please_enter')} disabled value="admin" className="text-sm text-text-primary" />
                        </div>
                        <Separator />
                        <div className="flex justify-between">
                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.username')}</Label>
                            <Input variant="ghost" placeholder={i18n._('common.please_enter')} disabled value="admin" className="text-sm text-text-primary" />
                        </div>
                        <Separator />
                        <div className="flex justify-between">
                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.username')}</Label>
                            <Input variant="ghost" placeholder={i18n._('common.please_enter')} disabled value="admin" className="text-sm text-text-primary" />
                        </div>
                        <Separator />
                        <div className="flex justify-between">
                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.username')}</Label>
                            <Input variant="ghost" placeholder={i18n._('common.please_enter')} disabled value="admin" className="text-sm text-text-primary" />
                        </div>
                        <Separator />
                        <div className="flex justify-between">
                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.username')}</Label>
                            <Input variant="ghost" placeholder={i18n._('common.please_enter')} disabled value="admin" className="text-sm text-text-primary" />
                        </div>
                        <Separator />
                        <div className="flex justify-between">
                            <Label className="text-sm text-text-primary shrink-0">{i18n._('sys.system_management.username')}</Label>
                            <Input variant="ghost" placeholder={i18n._('common.please_enter')} disabled value="admin" className="text-sm text-text-primary" />
                        </div>
                    </div>

                    <div className="flex gap-2 w-full mt-4 justify-end">
                        <Button variant="outline" className="">{i18n._('common.cancel')}</Button>
                        <Button variant="primary" className="">{i18n._('common.confirm')}</Button>
                    </div>
                </div>
            )}
        </div>
    )
}