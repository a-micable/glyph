#!/usr/bin/env python3
"""
Generate seed corpus for cache eviction fuzzer.
Creates complex structured inputs that trigger cache eviction paths.
"""

import os
import struct

def generate_seed_file(filename, config):
    """Generate a single seed file with given configuration."""
    data = bytearray()
    
    # max_entries (4 bytes)
    data.extend(struct.pack('<I', config['max_entries']))
    # max_bytes (4 bytes)
    data.extend(struct.pack('<I', config['max_bytes']))
    # num_inserts (4 bytes)
    data.extend(struct.pack('<I', config['num_inserts']))
    # bitmap_size (4 bytes)
    data.extend(struct.pack('<I', config['bitmap_size']))
    
    # Add pattern data for bitmap filling
    pattern = config.get('pattern', [0xAA, 0x55, 0xFF, 0x00])
    for i in range(100):
        data.append(pattern[i % len(pattern)])
    
    with open(filename, 'wb') as f:
        f.write(data)

def main():
    corpus_dir = "/home/amicable/Music/glyph/glyph-atlas/fuzz/corpus/cache_eviction_fuzzer"
    os.makedirs(corpus_dir, exist_ok=True)
    
    # Generate diverse seed cases
    seeds = [
        # Small cache, many insertions - triggers eviction
        {'max_entries': 10, 'max_bytes': 1024, 'num_inserts': 200, 'bitmap_size': 32, 'pattern': [0x01]},
        
        # Large cache, few insertions - no eviction
        {'max_entries': 10000, 'max_bytes': 10*1024*1024, 'num_inserts': 50, 'bitmap_size': 16, 'pattern': [0x02]},
        
        # Byte limit constraint
        {'max_entries': 1000, 'max_bytes': 4096, 'num_inserts': 500, 'bitmap_size': 64, 'pattern': [0x03]},
        
        # Entry limit constraint
        {'max_entries': 50, 'max_bytes': 10*1024*1024, 'num_inserts': 300, 'bitmap_size': 24, 'pattern': [0x04]},
        
        # Large bitmaps
        {'max_entries': 100, 'max_bytes': 1024*1024, 'num_inserts': 100, 'bitmap_size': 256, 'pattern': [0x05]},
        
        # Small bitmaps
        {'max_entries': 500, 'max_bytes': 512*1024, 'num_inserts': 1000, 'bitmap_size': 8, 'pattern': [0x06]},
        
        # Mixed pattern
        {'max_entries': 75, 'max_bytes': 2048*1024, 'num_inserts': 400, 'bitmap_size': 48, 'pattern': [0xAA, 0x55, 0xFF, 0x00]},
        
        # Edge case: exactly at limit
        {'max_entries': 100, 'max_bytes': 100*1024, 'num_inserts': 100, 'bitmap_size': 32, 'pattern': [0x07]},
        
        # Very large insertion count
        {'max_entries': 200, 'max_bytes': 2048*1024, 'num_inserts': 5000, 'bitmap_size': 20, 'pattern': [0x08]},
        
        # Zero bitmap size (sanitized to 32)
        {'max_entries': 50, 'max_bytes': 1024*1024, 'num_inserts': 100, 'bitmap_size': 0, 'pattern': [0x09]},
        
        # Maximum bitmap size
        {'max_entries': 50, 'max_bytes': 5*1024*1024, 'num_inserts': 50, 'bitmap_size': 512, 'pattern': [0x0A]},
        
        # Alternating pattern
        {'max_entries': 150, 'max_bytes': 1536*1024, 'num_inserts': 600, 'bitmap_size': 40, 'pattern': [0x01, 0x02, 0x03, 0x04]},
        
        # Random-like pattern
        {'max_entries': 120, 'max_bytes': 1280*1024, 'num_inserts': 450, 'bitmap_size': 36, 'pattern': [0x1A, 0x2B, 0x3C, 0x4D, 0x5E]},
        
        # Single value pattern
        {'max_entries': 80, 'max_bytes': 896*1024, 'num_inserts': 350, 'bitmap_size': 28, 'pattern': [0xFF]},
        
        # Complex eviction scenario
        {'max_entries': 25, 'max_bytes': 512*1024, 'num_inserts': 300, 'bitmap_size': 64, 'pattern': [0x10, 0x20, 0x30, 0x40]},
    ]
    
    for i, seed in enumerate(seeds):
        filename = os.path.join(corpus_dir, f"seed_{i:03d}.bin")
        generate_seed_file(filename, seed)
        print(f"Generated {filename}")
    
    print(f"\nGenerated {len(seeds)} seed files in {corpus_dir}")

if __name__ == "__main__":
    main()
