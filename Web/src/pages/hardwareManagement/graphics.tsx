import { useEffect, useState, useRef } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Label } from '@/components/ui/label';
import { Separator } from '@/components/ui/separator';
import { Skeleton } from '@/components/ui/skeleton';
import { Input } from '@/components/ui/input';
import { Switch } from '@/components/ui/switch';
import Slider from '@/components/slider';
import { useSystemInfo } from '@/store/systemInfo';
import deviceTool from '@/services/api/deviceTool';
import H264Player from '@/lib/MSE/h264Player';
import Loading from '@/components/loading';
import hardwareManagement from '@/services/api/hardware-management';
import { getWebSocketUrl } from '@/utils';

export default function Graphics() {
  const { i18n } = useLingui();
  const { deviceInfo } = useSystemInfo();
  const { getHardwareInfoReq, setHardwareInfoReq } = hardwareManagement;
  const { toggleAiReq, startVideoStreamReq, stopVideoStreamReq } = deviceTool;
  const [connectionStatus, setConnectionStatus] = useState('');
  const [brightness, setBrightness] = useState(0);
  const [contrast, setContrast] = useState(0);
  const [flipHorizontal, setFlipHorizontal] = useState(false);
  const [flipVertical, setFlipVertical] = useState(false);
  const [aec, setAec] = useState(0);
  const playerRef = useRef<HTMLVideoElement>(null);
  const [playerLoading, setPlayerLoading] = useState(false);
  const [loading, setLoading] = useState(false);
  let playerRender: H264Player | null = null;
  // ----------Skeleton screen
  const skeletonScreen = () => (
    <div className="flex flex-col gap-2 w-full">
      <div className="flex justify-between gap-4">
        <Skeleton className="w-10 h-8" />
        <Skeleton className="w-auto flex-1 h-8" />
      </div>
      <div className="flex justify-between gap-4">
        <Skeleton className="w-10 h-8" />
        <Skeleton className="w-auto flex-1 h-8" />
      </div>
      <div className="flex justify-between">
        <Skeleton className="w-10 h-8" />
        <Skeleton className="w-10 h-8" />
      </div>
      <div className="flex justify-between gap-2">
        <Skeleton className="w-10 h-8" />
        <Skeleton className="w-10 h-8" />
      </div>
    </div>
  );
  const initData = () => (deviceInfo?.camera_module
    ? setConnectionStatus('connected')
    : setConnectionStatus('disconnected'));
  const initPlayer = async () => {
    try {
      setPlayerLoading(true);
      await startVideoStreamReq();
      if (!playerRef.current) return;
      const video = playerRef.current;
      playerRender = new H264Player(() => { });
      playerRender.initPlayer(video);
      playerRender.start(getWebSocketUrl());
    } catch (error) {
      console.error(error);
    } finally {
      setPlayerLoading(false);
    }
  };
  useEffect(() => {
    (async () => {
      initData();
      await toggleAiReq({ ai_enabled: false });
      initPlayer();
    })();
    return () => {
      playerRender?.destroy();
      playerRender = null;
      stopVideoStreamReq();
    };
  }, [deviceInfo]);

  const initHardwareInfo = async () => {
    try {
      setLoading(true);
      const res = await getHardwareInfoReq();
      setBrightness(res.data.brightness);
      setContrast(res.data.contrast);
      setFlipHorizontal(res.data.horizontal_flip);
      setFlipVertical(res.data.vertical_flip);
      setAec(res.data.aec);
    } catch (error) {
      console.error(error);
    } finally {
      setLoading(false);
    }
  };
  useEffect(() => {
    initHardwareInfo();
  }, []);

  const handleSetHardwareInfo = async (type: string, value: number) => {
    // compute next state first to avoid sending outdated values
    let nextBrightness = brightness;
    let nextContrast = contrast;
    let nextFlipHorizontal = flipHorizontal;
    let nextFlipVertical = flipVertical;
    let nextAec = aec;

    switch (type) {
      case 'brightness':
        nextBrightness = value > 100 ? 100 : value < 0 ? 0 : value;
        setBrightness(nextBrightness);
        break;
      case 'contrast':
        nextContrast = value > 100 ? 100 : value < 0 ? 0 : value;
        setContrast(nextContrast);
        break;
      case 'flip_horizontal':
        nextFlipHorizontal = !flipHorizontal;
        setFlipHorizontal(nextFlipHorizontal);
        break;
      case 'flip_vertical':
        nextFlipVertical = !flipVertical;
        setFlipVertical(nextFlipVertical);
        break;
      case 'aec':
        nextAec = value;
        setAec(value);
        break;
      default:
        break;
    }

    try {
      await setHardwareInfoReq({
        brightness: nextBrightness,
        contrast: nextContrast,
        horizontal_flip: nextFlipHorizontal,
        vertical_flip: nextFlipVertical,
        aec: nextAec,
      });
    } catch (error) {
      console.error(error);
    }
  };

  return (
    <div>
      {loading && skeletonScreen()}
      {!loading && (
        <div
          className={`flex flex-col gap-2 mt-4 ${loading ? '' : 'bg-gray-100'} p-4 rounded-lg`}
        >
          <div className="flex justify-between">
            <Label>{i18n._('sys.hardware_management.connection_status')}</Label>
            <div className="flex items-center gap-2">
              <div
                className={`w-2 h-2 rounded-full ${connectionStatus === 'connected' ? 'bg-green-500' : 'bg-gray-500'}`}
              >
              </div>
              <p>{i18n._(`common.${connectionStatus}`)}</p>
            </div>
          </div>
          {connectionStatus === 'connected' && (
            <>
              <Separator />
              <div className="flex justify-between relative bg-black">
                <video
                  ref={playerRef as React.Ref<HTMLVideoElement>}
                  id="player"
                  className="w-full aspect-video"
                  muted
                  playsInline
                  disableRemotePlayback
                />
                {playerLoading && (
                  <div className="absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2">
                    <Loading placeholder="Loading..." />
                  </div>
                )}
              </div>
              <Separator />
              {loading ? (
                skeletonScreen()
              ) : (
                <>
                  <div className="flex justify-between gap-2 md:gap-4">
                    <Label>{i18n._('sys.hardware_management.aec')}</Label>
                    <Switch
                      checked={aec === 1}
                      onCheckedChange={(checked) => handleSetHardwareInfo('aec', checked ? 1 : 0)}
                    />
                  </div>
                { aec !== 1 && (
                    <>
                      <Separator />
                  <div className="flex justify-between gap-2 md:gap-4">
                    <div className="flex  items-center gap-2">
                      <Label>{i18n._('sys.hardware_management.brightness')}</Label>
                    </div>
                    <div className="flex grow-1 items-center gap-2">
                      <Slider
                        disabled={aec === 1}
                        className={` flex-1 ${aec === 1 ? 'opacity-50' : ''}`}
                        value={brightness}
                        onChange={value => setBrightness(value)}
                        onChangeEnd={value => handleSetHardwareInfo('brightness', value)}
                        max={100}
                        step={1}
                      />
                      <Input
                        className="w-[65px]"
                        type="number"
                        value={brightness}
                        min={0}
                        max={100}
                        step={1}
                        disabled={aec === 1}
                        onChange={e => {
                          const input = e.target as HTMLInputElement;
                          const n = Math.round(Number(input.value));
                          const clamped = Math.max(0, Math.min(100, Number.isFinite(n) ? n : 0));
                          input.value = String(clamped);
                          setBrightness(clamped);
                        }}
                        onBlur={e => handleSetHardwareInfo('brightness', Math.max(0, Math.min(100, Number.isNaN(Number((e.target as HTMLInputElement).value)) ? 0 : Number((e.target as HTMLInputElement).value))))}
                      />
                    </div>
                  </div>
                  <Separator />
                  <div className="flex justify-between gap-2 md:gap-4">
                    <div className="flex items-center gap-2">
                      <Label>{i18n._('sys.hardware_management.contrast')}</Label>
                    </div>
                    <div className="flex grow-1 items-center gap-2">
                      <Slider
                        disabled={aec === 1}
                        className={`flex-1 md:w-2xs ${aec === 1 ? 'opacity-50' : ''}`}
                        value={contrast > 100 ? 100 : contrast < 0 ? 0 : contrast}
                        onChange={value => setContrast(value)}
                        onChangeEnd={value => handleSetHardwareInfo('contrast', value)}
                        max={100}
                        step={1}
                      />
                      <Input
                        className="w-[65px]"
                        type="number"
                        value={contrast}
                        min={0}
                        max={100}
                        step={1}
                        disabled={aec === 1}
                        onChange={e => {
                          const input = e.target as HTMLInputElement;
                          const n = Math.round(Number(input.value));
                          const clamped = Math.max(0, Math.min(100, Number.isFinite(n) ? n : 0));
                          input.value = String(clamped);
                          setContrast(clamped);
                        }}
                        onBlur={e => handleSetHardwareInfo('contrast', Math.max(0, Math.min(100, Number.isNaN(Number((e.target as HTMLInputElement).value)) ? 0 : Number((e.target as HTMLInputElement).value))))}
                      />
                    </div>
                  </div>
                    </>
                  )}
                  <Separator />
                  <div className="flex justify-between">
                    <Label>
                      {i18n._('sys.hardware_management.flip_horizontal')}
                    </Label>
                    <Switch
                      checked={flipHorizontal}
                      onCheckedChange={() => handleSetHardwareInfo(
                        'flip_horizontal',
                        flipHorizontal ? 1 : 0
                      )}
                    />
                  </div>
                  <Separator />
                  <div className="flex justify-between">
                    <Label>
                      {i18n._('sys.hardware_management.flip_vertical')}
                    </Label>
                    <Switch
                      checked={flipVertical}
                      onCheckedChange={() => handleSetHardwareInfo(
                        'flip_vertical',
                        flipVertical ? 1 : 0
                      )}
                    />
                  </div>
                </>
              )}
            </>
          )}
        </div>
      )}
    </div>
  );
}
