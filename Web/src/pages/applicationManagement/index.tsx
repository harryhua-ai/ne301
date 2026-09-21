import { Card, CardContent } from '@/components/ui/card';
import { Tabs, TabsList, TabsTrigger, TabsContent } from '@/components/ui/tabs';
import MqttModule from './mqtt-module';
import WebhookModule from './webhook-module';
import LineCountingModule from './line-counting-module';

export default function ApplicationManagement() {
  return (
    <div className="flex justify-center">
      <Card className="sm:w-4xl w-full mx-4 my-4">
        <CardContent>
          <Tabs defaultValue="line-counting">
            <TabsList className="w-full">
              <TabsTrigger value="line-counting">过线统计</TabsTrigger>
              <TabsTrigger value="mqtt-module">MQTT/MQTTS</TabsTrigger>
              <TabsTrigger value="webhook-module">Webhook</TabsTrigger>
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
