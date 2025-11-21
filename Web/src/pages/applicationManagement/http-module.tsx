import { useState } from 'preact/hooks';
import { Label } from '@/components/ui/label';
import { Input } from '@/components/ui/input';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Button } from '@/components/ui/button';
import { Switch } from '@/components/ui/switch';
import { Separator } from '@/components/ui/separator';
import Upload from '@/components/upload';
import { useLingui } from '@lingui/react';
import SvgIcon from '@/components/svg-icon';

export default function HttpModule() {
    const { i18n } = useLingui();
    const [http, setHttp] = useState('http');
    const [connectionStatus] = useState('disconnected');
    const [serverAddress, setServerAddress] = useState('');
    const [port, setPort] = useState('');
    const [uri, setUri] = useState('');

    // MQTTS
    const [certVerify, setCertVerify] = useState(false);
    const [sni, setSni] = useState(false);
    const uploadSlot = (
        <>
        <SvgIcon icon="upload" />
        {i18n._('common.upload')}
        </>
    )
    const httpModule = () => (
        <div>
            <div className="flex flex-col mt-4 gap-2 bg-gray-100 p-4 rounded-lg mb-4">
                <div className="flex justify-between">
                    <Label>{i18n._('sys.application_management.cert_verify')}</Label>
                    <Switch checked={certVerify} onCheckedChange={setCertVerify} />
                </div>
                <Separator />
                <div className="flex justify-between">
                    <Label>{i18n._('sys.application_management.ca_cert')}</Label>
                    <Upload slot={uploadSlot} type="button" onFileChange={() => {}} />
                </div>
                <Separator />
                <div className="flex justify-between">
                    <Label>{i18n._('sys.application_management.client_cert')}</Label>
                    <Upload slot={uploadSlot} type="button" onFileChange={() => {}} />
                </div>
                <Separator />
                <div className="flex justify-between">
                    <Label>{i18n._('sys.application_management.private_key')}</Label>
                    <Upload slot={uploadSlot} type="button" onFileChange={() => {}} />
                </div>
            </div>
            <div className="flex flex-col gap-2 bg-gray-100 p-4 rounded-lg mt-4">
                <div className="flex justify-between">
                    <Label>{i18n._('sys.application_management.sni')}</Label>
                    <Switch checked={sni} onCheckedChange={setSni} />
                </div>
            </div>
        </div>
    )

return (
    <div>
        <div className="flex flex-col gap-2 mt-4 bg-gray-100 p-4 rounded-lg">
            <div className="flex gap-2 justify-between">
                <Label className="shrink-0">{i18n._('sys.application_management.protocol')}</Label>
                <Select value={http} onValueChange={setHttp}>
                    <SelectTrigger
                      value={http}
                      className="bg-transparent border-0 !shadow-none !outline-none
             focus:!outline-none focus:!ring-0 focus:!ring-offset-0 focus:!shadow-none focus:!border-transparent
             focus-visible:!outline-none focus-visible:!ring-0 focus-visible:!ring-offset-0
             text-right"
                    >
                        <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                        <SelectItem value="http">HTTP</SelectItem>
                        <SelectItem value="https">HTTPS</SelectItem>
                    </SelectContent>
                </Select>
            </div>
            <Separator />
            <div className="flex gap-2 justify-between">
                <Label className="shrink-0">{i18n._('sys.application_management.connection_status')}</Label>
                <div className="flex items-center gap-2">
                    <div className={`w-2 h-2 rounded-full ${connectionStatus === 'connected' ? 'bg-green-500' : 'bg-gray-500'}`}></div>
                    <p>{i18n._(`common.${connectionStatus}`)}</p>
                </div>
            </div>
            <Separator />
            <div className="flex gap-2 justify-between">
                <Label className="shrink-0">{i18n._('sys.application_management.server_address')}</Label>
                <Input
                  placeholder={i18n._('common.please_enter')}
                  variant="ghost"
                  value={serverAddress}
                  onChange={(e) => setServerAddress((e.target as HTMLInputElement).value)}
                />
            </div>
            <Separator />
            <div className="flex gap-2 justify-between">
                <Label className="shrink-0">{i18n._('sys.application_management.port')}</Label>

                <Input
                  placeholder={i18n._('common.please_enter')}
                  variant="ghost"
                  value={port}
                  onChange={(e) => setPort((e.target as HTMLInputElement).value)}
                />
            </div>
           <Separator />
           <div className="flex gap-2 justify-between">
            <Label className="shrink-0">URI</Label>
            <Input
              placeholder={i18n._('common.please_enter')}
              variant="ghost"
              value={uri}
              onChange={(e) => setUri((e.target as HTMLInputElement).value)}
            />
           </div>
          
        </div>
        {http === 'https' && httpModule()}
        <div className="">
            <Button variant="primary" className="w-20 mt-4">
                {connectionStatus === 'connected' ? i18n._('common.disconnect') : i18n._('common.connect')}
            </Button>
        </div>
    </div>
)
}