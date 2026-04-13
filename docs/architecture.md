tg-bot/
├── README.md
├── Makefile
├── .gitignore

├── config/
│   └── config.example.conf

├── include/
│   ├── config.h
│   ├── telegram.h
│   ├── commands.h
│   ├── logger.h
│   ├── utils.h
│   ├── security.h
│   ├── system.h
│   ├── services.h
│   ├── users.h
│   └── logs.h

├── src/
│   ├── main.c
│   ├── config.c
│   ├── telegram.c
│   ├── commands.c
│   ├── logger.c
│   ├── utils.c
│   ├── security.c
│   ├── system.c
│   ├── services.c
│   ├── users.c
│   └── logs.c

├── external/
│   └── cJSON/          # если используешь локально
│       ├── cJSON.c
│       └── cJSON.h

└── build/
    └── (bin, obj — опционально)
