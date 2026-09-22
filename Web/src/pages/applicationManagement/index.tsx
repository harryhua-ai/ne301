import { useLingui } from '@lingui/react';
import { Card, CardContent } from '@/components/ui/card';
import { Tabs, TabsList, TabsTrigger, TabsContent } from '@/components/ui/tabs';
import MqttModule from './mqtt-module';
import WebhookModule from './webhook-module';
import LineCountingModule from './line-counting-module';

export default function ApplicationManagement() {
  const { i18n } = useLingui();
  return (
    <div className="flex justify-center">
      <Card className="sm:w-4xl w-full mx-4 my-4">
        <CardContent>
          <Tabs defaultValue="line-counting">
            <TabsList className="w-full">
              <TabsTrigger value="line-counting">{i18n._('sys.application_management.line_counting_tab')}</TabsTrigger>
              <TabsTrigger value="mqtt-module">{i18n._('sys.application_management.mqtt_tab')}</TabsTrigger>
              <TabsTrigger value="webhook-module">{i18n._('sys.application_management.webhook_tab')}</TabsTrigger>
            </TabsList>
            <TabsContent value="line-counting">
              <LineCountingModule />
            </TabsContent>
            <TabsContent value="mqtt-module">
              <MqttModule />
            </TabsContent>
            <TabsContent value="webhook-module">
              <WebhookModule />
            </TabsContent>
          </Tabs>
        </CardContent>
      </Card>
    </div>
  );
}
