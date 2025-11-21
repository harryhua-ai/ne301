    import SvgIcon from '@/components/svg-icon'
    import { cn } from '@/lib/utils';
    import { Button } from '@/components/ui/button';
    import { useLingui } from '@lingui/react';
    import deviceTool from '@/services/api/deviceTool';
    import {
        Tooltip,
        TooltipContent,
        TooltipTrigger
    } from "@/components/tooltip"
    import { useAiStatusStore } from '@/store/aiStatus';

    type PlayerPanelProps = {
        handleReload: () => void;
        snapshot: () => void;
        className?: string;
        // showFps: () => void;
        // isShowFps?: boolean;
        isFullscreen: boolean;
        fullscreen: () => void;
        isControlPanel: boolean;
        isShowFps: boolean;
        showFps: () => void;
    }
    export default function PlayerPanel({ handleReload, isFullscreen, snapshot, fullscreen, isControlPanel, className, isShowFps, showFps }: PlayerPanelProps) {
        const { i18n } = useLingui();
        const { toggleAiReq } = deviceTool;
        const { isAiInference, setIsAiInference } = useAiStatusStore();

        const handleAiInferenceChange = async (value: boolean) => {
            try {
                await toggleAiReq({ ai_enabled: value })
                setIsAiInference(value)
            } catch (error) {
                console.error('handleAiInferenceChange', error)
                throw error
            }
        }
        return (
            <div className={cn("w-full md:h-[60px] h-[40px] md:px-12 px-8 flex items-center justify-between transition-all duration-300 ease-in-out bg-gradient-to-t from-black/70 via-black/30 to-transparent", className)}>
                <div className="flex items-center gap-4">
                    {/* <Button variant="ghost" onClick={handlePlay} className="md:w-8 w-6 md:h-8 h-6 !p-0 flex items-center justify-center">
                        {(isPlay && isControlPanel) ? <SvgIcon className="w-full !h-full flex-1" icon="pause" /> : <SvgIcon className="w-full !h-full flex-1" icon="play" />}
                    </Button> */}
                    <Tooltip>
                        <TooltipTrigger>
                            <Button variant="ghost" onClick={handleReload} className="md:w-8 w-6 md:h-8 h-6 !p-0 flex items-center justify-center">
                                <SvgIcon className="w-full !h-full flex-1" icon="reload" />
                            </Button>
                        </TooltipTrigger>
                        <TooltipContent>
                            {i18n._('sys.device_tool.reload')}
                        </TooltipContent>
                    </Tooltip>
                </div>
                <div className="flex gap-3">
                    <Tooltip>
                        <TooltipTrigger>
                            <div className="md:w-8 w-6 md:h-8 h-6 flex items-center justify-center">
                                <Button disabled={!isControlPanel} variant="ghost" onClick={() => handleAiInferenceChange(!isAiInference)} className="md:w-8 w-6 md:h-7 h-6 !p-0">
                                    {isAiInference ? <SvgIcon className="w-full !h-full flex-1" icon="ai" /> : <SvgIcon className="w-full !h-full flex-1 text-[#f3f2f3]" icon="ai_off" />}
                                </Button>
                            </div>

                        </TooltipTrigger>
                        <TooltipContent>
                            {i18n._('sys.device_tool.ai')}
                        </TooltipContent>
                    </Tooltip>

                    <Tooltip>
                        <TooltipTrigger>
                            <div className="md:w-8 w-6 md:h-8 h-6 flex items-center justify-center">
                                <Button disabled={!isControlPanel} variant="ghost" onClick={showFps} className="!p-0 !w-full !h-full">
                                    {isShowFps ? <SvgIcon className="!w-full !h-full flex-1 text-[#f3f2f3]" icon="show_fps" /> : <SvgIcon className="w-full !h-full flex-1 text-[#E6E6E6]" icon="close_fps" />}
                                </Button>
                            </div>
                        </TooltipTrigger>
                        <TooltipContent>
                            {i18n._('sys.device_tool.fps')}
                        </TooltipContent>
                    </Tooltip>

                    <Tooltip>
                        <TooltipTrigger>
                            <div className="md:w-8 w-6 md:h-8 h-6 flex items-center justify-center">
                                <Button disabled={!isControlPanel} variant="ghost" onClick={snapshot} className="!p-0 !w-full !h-full">
                                    <SvgIcon className="w-full !h-full flex-1" icon="screenshot" />
                                </Button>
                            </div>
                        </TooltipTrigger>
                        <TooltipContent>
                            {i18n._('sys.device_tool.snapshot')}
                        </TooltipContent>
                    </Tooltip>

                    <Tooltip>
                        <TooltipTrigger>
                            <div className="md:w-8 w-6 md:h-8 h-6 flex items-center justify-center">
                                <Button variant="ghost" onClick={fullscreen} className="!p-0 !w-full !h-full">
                                    {isFullscreen ? <SvgIcon className="w-full !h-full flex-1" icon="fullscreen_exit" /> : <SvgIcon className="w-full !h-full flex-1" icon="fullscreen" />}
                                </Button>
                            </div>
                        </TooltipTrigger>
                        <TooltipContent>
                            {i18n._('sys.device_tool.fullscreen')}
                        </TooltipContent>
                    </Tooltip>

                </div>
            </div>
        )
    }