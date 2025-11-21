// JMuxer type definitions
interface JMuxerConfig {
    node?: string | HTMLVideoElement;
    mode?: 'video' | 'audio' | 'both';
    flushingTime?: number;
    clearBuffer?: boolean;
    fps?: number;
    debug?: boolean;
    onReady?: () => void;
    onError?: (error: any) => void;
}

interface JMuxerFeedData {
    video?: ArrayBuffer | Uint8Array;
    audio?: ArrayBuffer;
    time?: number;
    iChannelId?: number;
    userData?: any;
}

interface JMuxer {
    feed(data: JMuxerFeedData): void;
    destroy(): void;
}

// Message type definitions
interface VideoWorkerMessage {
    cmd: 'stop' | 'video';
    data?: ArrayBuffer;
    videoTime?: number;
    iChannelId?: number;
    userData?: any;
}

// Global JMuxer declaration
declare const JMuxer: {
    new(config?: JMuxerConfig): JMuxer;
};

// import JMuxer from 'jmuxer';
// @ts-expect-error: importScripts is only available in worker context and jmuxer is UMD
importScripts('/libs/jmuxer.min.js');

const jmuxer: JMuxer = new JMuxer();
let animationFrameId: number | null = null;
let jmuxerCmd: MessageEvent<VideoWorkerMessage>[] = [];

function receiveMessage(event: MessageEvent<VideoWorkerMessage>): void {
    if (animationFrameId === null) {
        animationFrameId = requestAnimationFrame(dealJmuxerCmd);
        // eslint-disable-next-line no-console
        // console.log(`[VideoWorker] Create animation frame: ${animationFrameId}`);
    }
    jmuxerCmd.push(event);
}

function dealJmuxerCmd(): void {
    const len = jmuxerCmd.length;

    for (let i = 0; i < len; i += 1) {
        const [event] = jmuxerCmd.splice(0, 1);
        if (event === undefined) {
            break;
        }
        const msg = event.data;
        // eslint-disable-next-line no-console
        // console.log('[VideoWorker] Process message:', msg);

        switch (msg.cmd) {
            case 'stop': // Terminates the worker.
                jmuxer.destroy();
                if (animationFrameId !== null) {
                    cancelAnimationFrame(animationFrameId);
                    animationFrameId = null;
                }
                jmuxerCmd = [];
                // eslint-disable-next-line no-restricted-globals
                self.close();
                break;
            case 'video':
                if (msg.data) {
                    const videoBytes: Uint8Array = msg.data instanceof Uint8Array ? msg.data : new Uint8Array(msg.data);
                    if (videoBytes.byteLength === 0) break;

                    // JMuxer requires ArrayBuffer, ensure correct format is passed
                    // const videoBuffer = videoBytes.buffer.slice(videoBytes.byteOffset, videoBytes.byteOffset + videoBytes.byteLength);
                    jmuxer.feed({ video: videoBytes, time: msg.videoTime, iChannelId: msg.iChannelId, userData: msg.userData });
                }
                break;
            default:
                break;
        }
    }

    // If there are still pending messages, continue next frame
    if (jmuxerCmd.length > 0) {
        animationFrameId = requestAnimationFrame(dealJmuxerCmd);
    } else {
        animationFrameId = null;
    }
}

onmessage = receiveMessage;
