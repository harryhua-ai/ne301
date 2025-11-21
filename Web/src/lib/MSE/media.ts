// Type definitions
interface FrameData {
    data: ArrayBuffer;
    codec?: string;
}

interface CallbackEvent {
    t: 'mseError' | 'startPlay';
}

type CallbackFunction = (event: CallbackEvent) => void;

interface Mp4EventData {
    data: ArrayBuffer;
    codec: string;
}

type MediaSourceConstructor = {
    new (): MediaSource;
    isTypeSupported?(codec: string): boolean;
};
declare global {
    interface Window {
        ManagedMediaSource?: MediaSourceConstructor;
    }
}
class MsMediaSource {
    private mediaSource: MediaSource | null = null;

    private videoElement: HTMLVideoElement | null = null;

    private sourceBuffer: SourceBuffer | null = null;

    private frameBuffer: FrameData[] = [];

    private updateend: number = 1;

    private mimeCodec: string = "";

    private initFlag: number = 0;

    private removeOffset: number = 0;

    private cb: CallbackFunction;

    private currentSegmentIndex: number = 0;

    private skipDistance: number = MsMediaSource.skipCount;

    private isPlayback: boolean = false; // false: preview, true: playback

    constructor(cb: CallbackFunction) {
        this.cb = cb;
    }

    static get statusIdel(): number { return 0; }

    static get statusWait(): number { return 1; }

    static get statusNormal(): number { return 2; }

    static get statusError(): number { return 3; }

    static get statusDestroy(): number { return 4; }

    static get skipCount(): number { return 5; } // Frame skip catch-up count

    initMse(codec: string): boolean {
        // Unified selection of available MediaSource constructor (prefer ManagedMediaSource)
        const MediaSourceCtor = (window.ManagedMediaSource ?? window.MediaSource) as MediaSourceConstructor | undefined;
        if (!MediaSourceCtor) {
            console.error("MediaSource API is not supported!");
            return false;
        }

        // if (!window.MediaSource.isTypeSupported(codec)) {
        //     console.log(codec);
        //     console.error("Unsupported MIME type or codec: ", codec);
        //     return false;
        // }
        this.mimeCodec = codec;

        try {
            // create video
            this.videoElement?.addEventListener("error", this.videoErrorCallback.bind(this));

            // create mse
            this.mediaSource = new MediaSourceCtor();

            // video url
            if (this.videoElement) {
                this.videoElement.src = window.URL.createObjectURL(this.mediaSource);
            }

            // mse event
            this.mediaSource.addEventListener("sourceopen", () => {
                console.log("ms mse open.");
                this.uninitSourceBuffer();
                this.initSourceBuffer();
            });

            this.mediaSource.addEventListener("sourceclose", () => {
                console.log("ms mse close.");
            });

            this.mediaSource.addEventListener("sourceended", () => {
                console.log("ms mse ended.");
            });

            this.mediaSource.addEventListener("error", () => {
                console.log("ms mse error.");
            });

            this.mediaSource.addEventListener("abort", () => {
                console.log("ms mse abort.");
            });
        } catch (e) {
            console.log((e as Error).message);
            return false;
        }

        this.videoElement?.pause();
        return true;
    }

    videoErrorCallback(e: Event): void {
        try {
            const target = e.target as HTMLVideoElement;
            if (target?.error) {
                switch (target.error.code) {
                    case target.error.MEDIA_ERR_ABORTED:
                        console.error("video tag error : You aborted the media playback.");
                        break;
                    case target.error.MEDIA_ERR_NETWORK:
                        console.error("video tag error : A network error caused the media download to fail.");
                        break;
                    case target.error.MEDIA_ERR_DECODE:
                        console.error("video tag error : The media playback was aborted due to a corruption problem or because the media used features your browser did not support.");
                        break;
                    case target.error.MEDIA_ERR_SRC_NOT_SUPPORTED:
                        console.error("video tag error : The media could not be loaded, either because the server or network failed or because the format is not supported.");
                        break;
                    default:
                        console.error(`video tag error : An unknown media error occurred.${target.error.code}`);
                        break;
                }
            }

            // Mark as destroyed and notify external
            this.initFlag = MsMediaSource.statusDestroy;
            this.cb({ t: 'mseError' });

            // Try to reinitialize MSE (preserve existing mimeCodec and videoElement)
            const codec = this.mimeCodec;
            // First completely clean up to avoid residual state
            this.uninitMse();
            this.initFlag = MsMediaSource.statusIdel;
            if (codec) {
                // Slight delay to avoid immediate rebuild in the same event loop as error trigger
                setTimeout(() => {
                    if (this.initMse(codec)) {
                        this.initFlag = MsMediaSource.statusNormal;
                        this.removeOffset = 0;
                        // If there are buffered frames, continue driving playback
                        this.updateSourceBuffer();
                    } else {
                        this.initFlag = MsMediaSource.statusError;
                    }
                }, 300);
            }
        } catch (error) {
            console.log("videoErrorCallback error:", error);
        }
    }

    static makeBuffer(buffer1: Uint8Array, buffer2: Uint8Array): Uint8Array {
        const tmp = new Uint8Array(buffer1.byteLength + buffer2.byteLength);
        tmp.set(new Uint8Array(buffer1), 0);
        tmp.set(new Uint8Array(buffer2), buffer1.byteLength);
        return tmp;
    }

    initSourceBuffer(): number {
        if (this.sourceBuffer !== null) {
            console.log("source buffer already exist.");
            return -1;
        }

        if (!this.mediaSource) {
            return -1;
        }

        this.sourceBuffer = this.mediaSource.addSourceBuffer(this.mimeCodec);
        this.currentSegmentIndex = 0;
        const curMode = this.sourceBuffer.mode;
        if (curMode === 'segments') {
            this.sourceBuffer.mode = 'sequence';
        }
        this.skipDistance = MsMediaSource.skipCount;
        
        this.sourceBuffer.addEventListener("updateend", () => {
            try {
                if (this.sourceBuffer !== null && this.mediaSource?.readyState === 'open' && this.videoElement) {
                    const { buffered } = this.sourceBuffer;
                    // Guard: no ranges available
                    if (buffered.length === 0) {
                        this.updateend = 1;
                        return;
                    }
                    // Clamp currentSegmentIndex
                    if (this.currentSegmentIndex >= buffered.length) {
                        this.currentSegmentIndex = buffered.length - 1;
                    }
                    this.handleTimeUpdate();
                    const end = this.sourceBuffer.buffered.end(this.currentSegmentIndex);
                    const { currentTime } = this.videoElement;
                    
                    if (!this.isPlayback) {
                        if (end - currentTime >= 1 && this.skipDistance === 0) {
                            this.skipDistance = MsMediaSource.skipCount;
                        }
                        if (end - currentTime >= 0.5 && this.skipDistance) {
                            this.videoElement.currentTime = end - 0.4;
                            this.skipDistance--;
                        }
                    }
                    if (!this.sourceBuffer.updating && currentTime - this.removeOffset >= 20) {
                        this.sourceBuffer.remove(this.removeOffset, currentTime - 10);
                        this.removeOffset = currentTime - 10;
                    }
                }
            } catch (error) {
                console.log(error);
            }
            this.updateend = 1;
        });

        return 0;
    }

    handleTimeUpdate(): void {
        if (!this.sourceBuffer || !this.videoElement) return;
        
        const { buffered } = this.sourceBuffer;
        if (buffered.length === 0 || this.currentSegmentIndex === buffered.length - 1 || this.isPlayback) {
            return;
        }
        if (buffered.length && this.currentSegmentIndex >= buffered.length) {
            this.currentSegmentIndex = buffered.length - 1;
            return;
        }
        const { currentTime } = this.videoElement;
        const nextSegmentIndex = this.currentSegmentIndex + 1;
        const currentStart = buffered.start(this.currentSegmentIndex);
        const currentEnd = buffered.end(this.currentSegmentIndex);
        const nextStart = buffered.start(nextSegmentIndex);
        // const nextEnd = buffered.end(nextSegmentIndex);
        
        console.log(`currentTime=${currentTime}, currentStart=${currentStart}, currentEnd=${currentEnd}, nextStart=${nextStart}`);
        
        // If current time has exceeded next segment start time, delete current cache
        this.currentSegmentIndex += 1;
        this.videoElement.currentTime = nextStart;
        this.removeOffset = 0;
        this.sourceBuffer.remove(0, currentEnd);
        this.videoElement.play();
        this.skipDistance = MsMediaSource.skipCount;
    }

    uninitSourceBuffer(): void {
        if (this.sourceBuffer === null || !this.mediaSource) {
            return;
        }
        // this.sourceBuffer.removeEventListener("updateend", this.removeUpdateCallback);
        for (let i = 0; i < this.mediaSource.sourceBuffers.length; i++) {
            this.mediaSource.removeSourceBuffer(this.mediaSource.sourceBuffers[i]);
        }
        this.sourceBuffer = null;
    }

    updateSourceBuffer(): void {
        if (this.sourceBuffer === null || this.updateend !== 1 || this.sourceBuffer.updating) {
            return;
        }

        const len = this.frameBuffer.length;
        if (len === 0) {
            return;
        }

        let segmentBuffer = new Uint8Array();
        for (let i = 0; i < len; i++) {
            segmentBuffer = MsMediaSource.makeBuffer(segmentBuffer, new Uint8Array(this.frameBuffer[0].data));
            this.frameBuffer.shift();
            if (this.frameBuffer.length === 0) {
                break;
            }
        }

        try {
            this.sourceBuffer.appendBuffer(segmentBuffer);
            this.updateend = 0;
            if (this.videoElement?.paused) {
                this.videoElement.style.display = "";
                this.videoElement.play();
                this.cb({
                    t: 'startPlay',
                });
            }
        } catch (e) {
            console.error(`appending error: [update=${this.sourceBuffer.updating}, updateend=${this.updateend}, length=${len}, buffered.length=${this.sourceBuffer.buffered.length}]==>${e}`);
            this.initFlag = MsMediaSource.statusDestroy;
            this.cb({
                t: 'mseError',
            });
        }
    }

    processMp4VideoData(event: { data: Mp4EventData }, snapshotFlag: number): void {
        const objData = event.data;

        if (this.initFlag === MsMediaSource.statusIdel) {
            this.frameBuffer = [];
            this.initFlag = MsMediaSource.statusWait;
            if (this.initMse(objData.codec)) {
                this.initFlag = MsMediaSource.statusNormal;
                this.removeOffset = 0;
            } else {
                this.initFlag = MsMediaSource.statusError;
            }
        }

        if (document.hidden) {
            this.skipDistance = MsMediaSource.skipCount;
        }

        this.frameBuffer.push(objData);
        if (snapshotFlag === 0) {
            this.updateSourceBuffer();
        }
    }

    processMp4AudioData(event: { data: Mp4EventData }): void {
        const objData = event.data;

        if (this.initFlag === MsMediaSource.statusIdel) {
            this.frameBuffer = [];
            this.initFlag = MsMediaSource.statusWait;
            if (this.initMse(objData.codec)) {
                this.initFlag = MsMediaSource.statusNormal;
                this.removeOffset = 0;
            } else {
                this.initFlag = MsMediaSource.statusError;
            }
        }

        this.frameBuffer.push(objData);
        this.updateSourceBuffer();
    }

    setVideoElement(video: HTMLVideoElement): void {
        this.videoElement = video;
    }

    setPlayMode(playback: boolean): void {
        this.isPlayback = playback;
    }

    uninitMse(): void {
        if (this.videoElement !== null) {
            this.videoElement.removeEventListener("error", this.videoErrorCallback);
            window.URL.revokeObjectURL(this.videoElement.src);
            this.videoElement.src = "";
        }

        this.uninitSourceBuffer();
        this.mediaSource = null;
        this.videoElement = null;
        this.sourceBuffer = null;
        this.frameBuffer = [];
        this.updateend = 1;
        this.removeOffset = 0;
        this.mimeCodec = "";
        this.initFlag = MsMediaSource.statusIdel;
    }
}

export default MsMediaSource;