.PHONY: firmware ai run clean push help

# Build firmware ESP32 (PlatformIO, không upload)
firmware:
	python -m platformio run

# Chạy service AI Python với config từ .env
ai:
	export $$(xargs < .env); python src/backup_air_data.py

# Chạy full: build firmware + chạy AI
run: firmware ai

# Dọn dẹp build + file sinh ra
clean:
	-python -m platformio run -t clean
	rm -rf air_outputs
	rm -f backup*.json

# Push code lên git và in nội dung commit cuối cùng
push:
	git push
	@git log -1 --pretty=full

# Hiển thị hướng dẫn lệnh make
help:
	@echo "Targets khả dụng:"
	@echo "  make firmware  - Build firmware ESP32 (PlatformIO)"
	@echo "  make ai        - Chạy service AI Python với .env"
	@echo "  make run       - Build firmware + chạy AI"
	@echo "  make clean     - Dọn dẹp build và file sinh ra"
	@echo "  make push      - Git push và in nội dung commit cuối"
