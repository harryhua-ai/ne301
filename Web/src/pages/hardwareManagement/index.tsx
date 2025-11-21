import { Card, CardContent } from '@/components/ui/card';
import { Tabs, TabsList, TabsTrigger, TabsContent } from '@/components/ui/tabs';
import Graphics from './graphics';
import Light from './light';
import { useLingui } from '@lingui/react';

export default function ApplicationManagement() {
  const { i18n } = useLingui();
return (
   <div className="flex justify-center">
    <Card className="sm:w-xl w-full mx-4 my-4">
      <CardContent>
        <Tabs defaultValue="graphics">
          <TabsList className="w-full">
            <TabsTrigger value="graphics">{i18n._('sys.hardware_management.image_title')}</TabsTrigger>
            <TabsTrigger value="light">{i18n._('sys.hardware_management.light_title')}</TabsTrigger>
          </TabsList>
          <TabsContent value="graphics">
            <Graphics />
          </TabsContent>
          <TabsContent value="light">
            <Light />
          </TabsContent>
        </Tabs>
      </CardContent>
    </Card>
   </div>
)
} 