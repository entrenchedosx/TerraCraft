"""Render worldgen_probe CSV/PPM output. Requires numpy and matplotlib."""
import argparse
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap

parser = argparse.ArgumentParser()
parser.add_argument("prefix", type=Path)
args = parser.parse_args()
prefix = str(args.prefix)
data = np.genfromtxt(prefix + "-fields.csv", delimiter=",", names=True)
side = int(np.sqrt(data.size))
fields = ["height", "biome", "continentalness", "erosion", "temperature", "humidity", "weirdness", "mountain", "river"]
fig, axes = plt.subplots(3, 3, figsize=(15, 13), constrained_layout=True)
biomes = ListedColormap(["#2467a0", "#d7c688", "#8fa651", "#34764b", "#d6aa61", "#d9e6ef", "#88828b"])
for ax, field in zip(axes.flat, fields):
    values = data[field].reshape(side, side)
    palette = "terrain" if field == "height" else biomes if field == "biome" else "coolwarm"
    img = ax.imshow(values, origin="lower", extent=(-2048, 2048, -2048, 2048), cmap=palette,
                    vmin=0 if field == "biome" else None, vmax=6 if field == "biome" else None)
    ax.set_title(field)
    fig.colorbar(img, ax=ax, shrink=.75)
fig.suptitle("TerraCraft profile 4 — 4096 × 4096 blocks, 8-block samples")
fig.savefig(prefix + "-atlas.png", dpi=130)
plt.close(fig)
from PIL import Image
Image.open(prefix + "-section.ppm").resize((1536, 768)).save(prefix + "-section.png")
Image.open(prefix + "-region.ppm").resize((1024, 1024), Image.Resampling.NEAREST).save(prefix + "-region.png")
section = np.genfromtxt(prefix + "-density.csv", delimiter=",", names=True)
fig, axes = plt.subplots(2, 4, figsize=(16, 7), constrained_layout=True)
for ax, field in zip(axes.flat, ["terrain", "cheese", "spaghetti", "noodle", "final_density", "aquifer_level", "barrier", "vein_ridge"]):
    values = section[field].reshape(128, 256)
    img = ax.imshow(values, origin="lower", extent=(-256, 256, 0, 256), aspect="auto", cmap="coolwarm")
    if field in ["terrain", "cheese", "spaghetti", "noodle", "final_density"]:
        ax.contour(values, levels=[0], origin="lower", extent=(-256, 256, 0, 256), colors="black", linewidths=.5)
    ax.set_title(field)
    fig.colorbar(img, ax=ax, shrink=.75)
fig.savefig(prefix + "-density.png", dpi=130)
plt.close(fig)
print(prefix + "-atlas.png")
