#!/usr/bin/env python3
import socket
import sys
import os
import time
import hashlib

HOST = "192.168.4.1"
PORT = 3232
PASSWORD = "schoenh3ll"
FIRMWARE = ".pio/build/esp32dev-recovery/firmware.bin"
CHUNK_SIZE = 512  # SEHR KLEIN - langsam aber sicher
RETRY_ATTEMPTS = 3

def upload():
    if not os.path.exists(FIRMWARE):
        print(f"ERROR: {FIRMWARE} not found")
        print(f"Build firmware first: pio run -e esp32dev-recovery")
        return False
    
    with open(FIRMWARE, 'rb') as f:
        data = f.read()
    
    size = len(data)
    md5 = hashlib.md5(data).hexdigest()
    
    print(f"\n=== OTA Upload Configuration ===")
    print(f"Firmware: {FIRMWARE}")
    print(f"Size: {size} bytes ({size/1024:.1f} KB)")
    print(f"MD5: {md5}")
    print(f"Host: {HOST}:{PORT}")
    print(f"Chunk size: {CHUNK_SIZE} bytes")
    print(f"================================\n")
    
    for attempt in range(RETRY_ATTEMPTS):
        if attempt > 0:
            print(f"\n>>> Retry attempt {attempt + 1}/{RETRY_ATTEMPTS} <<<")
            time.sleep(3)
        
        print(f"Connecting to {HOST}:{PORT}...")
        
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(30)
        
        try:
            sock.connect((HOST, PORT))
            print("✓ Connected")
            
            # Send header
            header = f"0 {size} {md5} {PASSWORD}\n"
            print(f"Sending header: {header.strip()}")
            sock.sendall(header.encode())
            
            # Wait for OK
            resp = sock.recv(1024).decode().strip()
            print(f"Response: {resp}")
            
            if "OK" not in resp:
                print(f"✗ ERROR: Expected OK, got '{resp}'")
                sock.close()
                continue
            
            print("✓ Header accepted, starting upload...")
            
            # Send data in TINY chunks
            sent = 0
            last_percent = -1
            start_time = time.time()
            
            while sent < size:
                chunk = data[sent:sent + CHUNK_SIZE]
                sock.sendall(chunk)
                sent += len(chunk)
                
                progress = (sent / size) * 100
                if int(progress) != last_percent:
                    elapsed = time.time() - start_time
                    speed = sent / elapsed / 1024 if elapsed > 0 else 0
                    eta = (size - sent) / (speed * 1024) if speed > 0 else 0
                    print(f"\rProgress: {progress:.1f}% ({sent}/{size} bytes) | Speed: {speed:.1f} KB/s | ETA: {eta:.0f}s", end='')
                    last_percent = int(progress)
                
                # Small delay for stability
                time.sleep(0.01)
            
            print("\n✓ Upload complete!")
            print(f"Total time: {time.time() - start_time:.1f}s")
            sock.close()
            return True
            
        except socket.timeout:
            print(f"\n✗ Socket timeout after 30s")
        except ConnectionRefusedError:
            print(f"\n✗ Connection refused - is ESP32 in OTA mode?")
        except BrokenPipeError:
            print(f"\n✗ Connection broken during upload")
        except Exception as e:
            print(f"\n✗ Error: {e}")
        
        try:
            sock.close()
        except:
            pass
    
    print(f"\n✗ All {RETRY_ATTEMPTS} attempts failed")
    return False

if __name__ == "__main__":
    print("=== Slow OTA Uploader ===")
    print("Press Ctrl+C to abort\n")
    
    try:
        success = upload()
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("\n\nAborted by user")
        sys.exit(1)
