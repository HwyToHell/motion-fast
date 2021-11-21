import sys
import pyrebase
from datetime import datetime, date, timedelta

def get_files_to_remove(file_list, days_in_history):
    files_to_delete = []
    days_history = timedelta(days=days_in_history)

    today = datetime.now()
    date_today_midnight = datetime(today.year, today.month, today.day)

    for filename in files:
        filename_split = filename.split('_', maxsplit=1)
        print(f"\nfilename {filename} split by '_' in {filename_split[0]}")

        try:
            date_from_file = datetime.strptime(filename_split[0], "%Y-%m-%d")
            date_diff = date_today_midnight - date_from_file
            print(date_diff)
            if (date_diff) > days_history:
                print(f"delete file from {date_diff} before")
                files_to_delete.append(filename)
        except ValueError as e:
            print(f"Error parsing time from filename: {filename}\n{e}")

    return files_to_delete


files = ["2021-11-10_14h00m00s.mp4", "2021-11-15_14h00m00s.mp4", "2021-11-16_14h00m00s.mp4", "2021-11-2014h00m00s.mp4", "_videos"]
backlog_remove = get_files_to_remove(files, 5)

print()
print(backlog_remove)
