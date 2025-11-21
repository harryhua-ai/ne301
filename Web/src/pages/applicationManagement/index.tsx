import { Card, CardContent } from '@/components/ui/card';
import { Tabs, TabsList, TabsTrigger, TabsContent } from '@/components/ui/tabs';
import MqttModule from './mqtt-module';
// import HttpModule from './http-module';

export default function ApplicationManagement() {
return (
   <div className="flex justify-center">
    <Card className="sm:w-xl w-full mx-4 my-4">
      <CardContent>
        <Tabs defaultValue="mqtt-module">
          <TabsList className="w-full">
            <TabsTrigger value="mqtt-module">MQTT/MQTTS</TabsTrigger>
            {/* <TabsTrigger value="http-module">HTTP/HTTPS</TabsTrigger> */}
          </TabsList>
          <TabsContent value="mqtt-module">
            <MqttModule />
          </TabsContent>
          {/* <TabsContent value="http-module">
            <HttpModule />
          </TabsContent> */}
        </Tabs>
      </CardContent>
    </Card>
   </div>
)
} 