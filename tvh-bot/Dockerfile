FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONUNBUFFERED=1

WORKDIR /app

COPY pyproject.toml ./
COPY tvh_bot ./tvh_bot

RUN pip install --no-cache-dir .

CMD ["python", "-m", "tvh_bot.main"]
