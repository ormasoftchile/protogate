from setuptools import setup, find_packages

with open("README.md", "r", encoding="utf-8") as fh:
    long_description = fh.read()

setup(
    name="protogate-tunnel-agent",
    version="1.0.0",
    author="Protogate Team",
    description="Tunnel agent client for Protogate reverse tunnel server",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/ormasoftchile/protogate",
    packages=find_packages(),
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "Topic :: System :: Networking",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
    ],
    python_requires=">=3.8",
    install_requires=[
        "h2>=4.1.0",
        "httpx>=0.25.0",
        "pyyaml>=6.0",
        "click>=8.1.0",
    ],
    entry_points={
        "console_scripts": [
            "tunnel-agent=agent.main:cli",
        ],
    },
)
