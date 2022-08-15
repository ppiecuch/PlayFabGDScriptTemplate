FROM node

WORKDIR /app/

RUN mkdir -p /app

RUN git clone https://github.com/PlayFab/SDKGenerator

COPY make.js /app/SDKGenerator/privateTemplates/godot/
COPY source.cpp /app/SDKGenerator/privateTemplates/godot/source
COPY templates /app/SDKGenerator/privateTemplates/godot/templates

RUN ls -l /app

WORKDIR /app/SDKGenerator

CMD ["node", "generate.js", "godot=/app"]
