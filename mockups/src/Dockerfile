FROM python:3.12-slim
RUN apt-get update && apt-get install -y --no-install-recommends fonts-dejavu-core fonts-liberation && rm -rf /var/lib/apt/lists/*
RUN pip install --no-cache-dir "ezdxf==1.4.4" matplotlib pillow
WORKDIR /work
