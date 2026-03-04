/* eslint-disable jsx-a11y/media-has-caption */
import React, { useEffect, useRef, useState } from 'react';

// MediaMTX WebRTC WHEP endpoints
const CAMERA_URLS = ['http://10.0.0.2:8889/video0/', 'http://10.0.0.2:8889/video1/', 'http://10.0.0.2:8889/video2/'];

const WebRTCStream = () => {
  const [connectedCount, setConnectedCount] = useState(0);
  const videoRefs = [
    useRef<HTMLVideoElement | null>(null),
    useRef<HTMLVideoElement | null>(null),
    useRef<HTMLVideoElement | null>(null),
  ];
  const peerConnections = useRef<RTCPeerConnection[]>([]);

  useEffect(() => {
    console.log('WebRTC Component Mounted - Connecting directly to MediaMTX');

    // Connect to each camera stream
    CAMERA_URLS.forEach((url, index) => {
      connectToMediaMTX(url, index);
    });

    return () => {
      // Cleanup all peer connections
      peerConnections.current.forEach((pc) => {
        if (pc) pc.close();
      });
      console.log('[WebRTC] All connections closed');
    };
  }, []);

  const connectToMediaMTX = async (baseUrl: string, index: number) => {
    try {
      const pc = new RTCPeerConnection({
        iceServers: [{ urls: 'stun:stun.l.google.com:19302' }],
      });
      peerConnections.current[index] = pc;

      pc.ontrack = (event) => {
        console.log(`Track received for camera ${index}`);
        if (videoRefs[index].current) {
          videoRefs[index].current.srcObject = event.streams[0];
          setConnectedCount((prev) => prev + 1);
        }
      };

      pc.oniceconnectionstatechange = () => {
        console.log(`Camera ${index} ICE state: ${pc.iceConnectionState}`);
      };

      // Add transceiver for receiving video
      pc.addTransceiver('video', { direction: 'recvonly' });

      // Create offer
      const offer = await pc.createOffer();
      await pc.setLocalDescription(offer);

      // Wait for ICE gathering to complete
      await new Promise<void>((resolve) => {
        if (pc.iceGatheringState === 'complete') {
          resolve();
        } else {
          pc.onicegatheringstatechange = () => {
            if (pc.iceGatheringState === 'complete') {
              resolve();
            }
          };
        }
      });

      // Send offer to MediaMTX WHEP endpoint
      const response = await fetch(`${baseUrl}whep`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/sdp',
        },
        body: pc.localDescription?.sdp,
      });

      if (response.ok) {
        const answerSdp = await response.text();
        await pc.setRemoteDescription(
          new RTCSessionDescription({
            type: 'answer',
            sdp: answerSdp,
          }),
        );
        console.log(`Camera ${index} connected successfully!`);
      } else {
        console.error(`Failed to connect to camera ${index}: ${response.status}`);
      }
    } catch (error) {
      console.error(`Error connecting to camera ${index}:`, error);
    }
  };

  const getGridStyle = () => {
    if (connectedCount === 1) {
      return { gridTemplateColumns: '1fr' };
    }
    if (connectedCount === 2) {
      return { gridTemplateColumns: '1fr 1fr' };
    }
    return { gridTemplateColumns: '1fr 1fr', gridTemplateRows: '1fr 1fr' };
  };

  return (
    <div className='p-4 h-screen'>
      {connectedCount === 0 && (
        <div
          style={{
            backgroundColor: 'white',
            padding: '20px',
            fontSize: '24px',
            textAlign: 'center',
            borderRadius: '15px',
            position: 'fixed',
            top: '50%',
            left: '50%',
            transform: 'translate(-50%, -50%)',
            boxShadow: '0 4px 8px rgba(0, 0, 0, 0.2)',
            zIndex: 1000,
          }}
        >
          Connecting to cameras...
        </div>
      )}
      <div
        className='grid gap-2 h-full'
        style={{
          display: 'grid',
          ...getGridStyle(),
        }}
      >
        {videoRefs.map((ref, index) => (
          <video
            key={index}
            ref={ref}
            autoPlay
            playsInline
            muted
            style={{ width: '100%', height: '100%', objectFit: 'contain', backgroundColor: '#000' }}
          />
        ))}
      </div>
    </div>
  );
};

export default WebRTCStream;
