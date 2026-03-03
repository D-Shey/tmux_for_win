
import os
import time

def check_pipes():
    print("Checking for tmux pipes...")
    pipes = [f for f in os.listdir(r'\\.\pipe\\') if 'tmux' in f.lower()]
    for p in pipes:
        print(f"Found pipe: {p}")
    if not pipes:
        print("No tmux pipes found.")

def check_processes():
    print("\nChecking for tmux processes...")
    output = os.popen('tasklist /V').read()
    found = False
    for line in output.splitlines():
        if 'tmux' in line.lower():
            print(line)
            found = True
    if not found:
        print("No tmux processes found in tasklist /V.")

if __name__ == "__main__":
    check_pipes()
    check_processes()
