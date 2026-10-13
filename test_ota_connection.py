#!/usr/bin/env python3
"""
Test verschiedene Wege um OTA-Verbindung zum ESP32 herzustellen
"""
import socket
import subprocess
import sys

HOST = "192.168.4.1"
HOSTNAME = "dmxbox4.local"

def test_ping(host):
    """Teste ob Host erreichbar ist"""
    print(f"\n=== Testing Ping to {host} ===")
    try:
        result = subprocess.run(['ping', '-c', '3', host], 
                              capture_output=True, text=True, timeout=10)
        print(result.stdout)
        return result.returncode == 0
    except Exception as e:
        print(f"Error: {e}")
        return False

def test_http(host):
    """Teste HTTP-Verbindung"""
    print(f"\n=== Testing HTTP to {host} ===")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        result = sock.connect_ex((host, 80))
        sock.close()
        if result == 0:
            print("✓ Port 80 (HTTP) is OPEN")
            return True
        else:
            print("✗ Port 80 (HTTP) is CLOSED")
            return False
    except Exception as e:
        print(f"Error: {e}")
        return False

def test_ota_port(host, port):
    """Teste OTA-Port"""
    print(f"\n=== Testing OTA Port {port} on {host} ===")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        result = sock.connect_ex((host, port))
        sock.close()
        if result == 0:
            print(f"✓ Port {port} is OPEN")
            return True
        else:
            print(f"✗ Port {port} is CLOSED (Error code: {result})")
            return False
    except Exception as e:
        print(f"Error: {e}")
        return False

def scan_common_ports(host):
    """Scanne häufige Ports"""
    print(f"\n=== Scanning Common Ports on {host} ===")
    common_ports = [80, 3232, 8266, 8080, 23, 22]
    open_ports = []
    
    for port in common_ports:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(1)
            result = sock.connect_ex((host, port))
            sock.close()
            if result == 0:
                print(f"✓ Port {port} is OPEN")
                open_ports.append(port)
            else:
                print(f"  Port {port} is closed")
        except:
            pass
    
    return open_ports

def test_mdns():
    """Teste mDNS Auflösung"""
    print(f"\n=== Testing mDNS Resolution ===")
    try:
        result = subprocess.run(['ping', '-c', '1', HOSTNAME], 
                              capture_output=True, text=True, timeout=5)
        if result.returncode == 0:
            print(f"✓ {HOSTNAME} resolves via mDNS")
            print(result.stdout)
            return True
        else:
            print(f"✗ {HOSTNAME} does NOT resolve via mDNS")
            return False
    except Exception as e:
        print(f"Error: {e}")
        return False

if __name__ == "__main__":
    print("=" * 60)
    print("OTA Connection Diagnostic Tool")
    print("=" * 60)
    
    # Test 1: Ping
    ping_ok = test_ping(HOST)
    
    # Test 2: HTTP
    http_ok = test_http(HOST)
    
    # Test 3: OTA Port
    ota_ok = test_ota_port(HOST, 3232)
    
    # Test 4: Scan ports
    open_ports = scan_common_ports(HOST)
    
    # Test 5: mDNS
    mdns_ok = test_mdns()
    
    # Summary
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)
    print(f"Ping to {HOST}:        {'✓ OK' if ping_ok else '✗ FAILED'}")
    print(f"HTTP (port 80):        {'✓ OK' if http_ok else '✗ FAILED'}")
    print(f"OTA (port 3232):       {'✓ OK' if ota_ok else '✗ FAILED'}")
    print(f"mDNS ({HOSTNAME}):     {'✓ OK' if mdns_ok else '✗ FAILED'}")
    print(f"Open ports found:      {open_ports if open_ports else 'None'}")
    
    if not ota_ok:
        print("\n⚠️  OTA port 3232 is not reachable!")
        print("Possible causes:")
        print("  - ESP32 is not running OTA service")
        print("  - Firewall blocking port 3232")
        print("  - ESP32 crashed/stuck")
        print("  - Wrong WiFi network")
        
        if http_ok:
            print("\n💡 Port 80 is open - try web-based upload instead")
    
    sys.exit(0 if ota_ok else 1)
