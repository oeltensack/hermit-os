# Pagurus Sprachdienst: Spracherkennung (faster-whisper, MIT) und Sprachausgabe (Piper, GPL-3.0)
FROM python:3.12-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY server.py .
ENV VOICE_DATA=/data PYTHONUNBUFFERED=1 HF_HUB_DISABLE_TELEMETRY=1
EXPOSE 10300
CMD ["uvicorn", "server:app", "--host", "0.0.0.0", "--port", "10300"]
