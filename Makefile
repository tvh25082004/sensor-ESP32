.PHONY: firmware ai run

# Build firmware ESP32 (PlatformIO, không upload)
firmware:
	platformio run

# Chạy service AI Python với config từ .env
ai:
	export $$(xargs < .env); python src/backup_air_data.py

# Chạy full: build firmware + chạy AI
run: firmware ai

