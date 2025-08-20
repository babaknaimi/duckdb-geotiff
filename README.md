# geotiff (DuckDB Community Extension)

`geotiff` lets DuckDB read GeoTIFF rasters via GDAL and expose them as a table function.

## Install

```sql
INSTALL geotiff FROM community;
LOAD geotiff;
```


(If you installed an older copy locally and want to refresh:)

```sql
FORCE INSTALL geotiff FROM community;
LOAD geotiff;
```

# Usage

## Single band (long form)

Returns two columns:

- cell_id BIGINT — 0-based linear index in row-major order (row * width + col)

- value DOUBLE — pixel value (NULL for NoData)

```sql
SELECT * FROM read_geotiff('cea.tif', band := 1) LIMIT 5;
```

## Multiple bands (wide form)

Returns one row per cell with one column per requested band:

```sql
SELECT * FROM read_geotiff('cea.tif', band := [1,2,3]) LIMIT 5;
-- schema: (cell_id BIGINT, band1 DOUBLE, band2 DOUBLE, band3 DOUBLE)

```

## Typical patterns

### Create a wide table from a multi-band raster:

```sql
CREATE TABLE r_chelsa AS
SELECT * FROM read_geotiff('cea.tif', band := [1,2,3]);
CREATE INDEX idx_r_chelsa_cell ON r_chelsa(cell_id);

```

### Add one more band as a new column:
```sql
ALTER TABLE r_chelsa ADD COLUMN IF NOT EXISTS band4 DOUBLE;
UPDATE r_chelsa t
SET band4 = g.band4
FROM read_geotiff('cea.tif', band := [4]) g
WHERE t.cell_id = g.cell_id;

```

### Filter/aggregate:

```sql
-- mean of band2 over all cells
SELECT avg(band2) FROM r_chelsa;

-- spatial subset: pick a range of cell_ids
SELECT * FROM r_chelsa WHERE cell_id BETWEEN 1e6 AND 1e6 + 999;

```
## Arguments:

- band LIST<INTEGER> – read a single or multiple band(s) and return a wide table

- target_mb INTEGER – approximate in-memory window size (MB) used to batch raster I/O
and reduce GDAL call overhead. Defaults to 64; increase (e.g. 256–1024) on big machines
to reduce passes over the file. The extension chooses a block-aligned number of rows.

### Notes:

The function streams; it does not load the full raster in memory.

NoData values are returned as NA.


### R example:

```r
library(duckdb)

con <- dbConnect(duckdb::duckdb())

dbExecute(con, "INSTALL geotiff FROM community;")
dbExecute(con, "LOAD geotiff;")

# Single band
dbGetQuery(con, "SELECT * FROM read_geotiff('cea.tif', band := 1) LIMIT 5;")

# Multiple bands
dbGetQuery(con, "SELECT * FROM read_geotiff('cea.tif', band := [1,2,3]) LIMIT 5;")

dbDisconnect(con, shutdown = TRUE)

```

### Performance tips:

Tune target_mb upward if you have RAM and want fewer GDAL calls.

Create an index on cell_id after your final load for faster random access.
