FROM python:3.12-slim

WORKDIR /app

COPY server/requirements.txt /app/server/requirements.txt
RUN pip install --no-cache-dir -r /app/server/requirements.txt

COPY tools/ /app/tools/
COPY server/ /app/server/
COPY tools/ /app/tools/
COPY docs/ /app/docs/
COPY shared/ /app/shared/
COPY firmware-artifacts/ /app/firmware-artifacts/
COPY flasher/ /app/flasher/

WORKDIR /app/server

EXPOSE 25555

CMD ["python", "app.py"]
