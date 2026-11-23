FROM python:3.12-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY *.py version.json ./
COPY static ./static
ENV DATA_DIR=/data PYTHONUNBUFFERED=1
EXPOSE 8080
CMD ["uvicorn", "hermit_cloud:app", "--host", "0.0.0.0", "--port", "8080", "--proxy-headers"]
