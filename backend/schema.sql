CREATE TABLE IF NOT EXISTS leituras (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT NOT NULL,
    temperatura_c REAL NOT NULL CHECK (temperatura_c BETWEEN -100 AND 150),
    umidade_pct REAL NOT NULL CHECK (umidade_pct BETWEEN 0 AND 100),
    luminosidade INTEGER NOT NULL CHECK (luminosidade >= 0),
    presenca INTEGER NOT NULL CHECK (presenca IN (0, 1)),
    probabilidade_vida REAL NOT NULL CHECK (probabilidade_vida BETWEEN 0 AND 100)
);

CREATE INDEX IF NOT EXISTS idx_leituras_timestamp
    ON leituras (timestamp);
