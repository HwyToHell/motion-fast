from datetime import datetime

print(f"timestamp {datetime.now()}")

print(f"timestamp {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")

def time_stamp():
    return datetime.now().strftime('%Y-%m-%d %H:%M:%S')


print(time_stamp(), "command")