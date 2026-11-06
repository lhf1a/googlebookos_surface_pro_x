#!/usr/bin/env python3
"""Write a copy: consume TX DMA ownership once, including late completion IRQs.

The ISR has already acknowledged TX_DMA_DONE. A canceled transfer has zero
outstanding bytes; unmapping/advancing it again is invalid. This is not a PM
success override and does not bypass a real nonzero transfer's DMA unmap.
"""
from pathlib import Path
import argparse

def patch(s):
 # 6.18 uses a kfifo and already protects flush-induced underflow. Keep that
 # logic; only fence the ownership-consuming block on an outstanding count.
 if 'bool tx_dma_stale = port->tx_dma_stale;' in s:
  start=s.index('\t/*\n\t * Only advance the kfifo', s.index('static void qcom_geni_serial_handle_tx_dma'))
  end=s.index('\n\tport->tx_dma_stale = false;',start)
  block=s[start:end]
  if 'if (port->tx_remaining)' in block:raise ValueError('Already protected')
  if block.count('geni_se_tx_dma_unprep(')!=1:raise ValueError('Unknown GENI ownership block')
  change='\t/* A late DONE after cancellation no longer owns a DMA mapping. */\n\tif (port->tx_remaining) {\n'+''.join('\t'+line if line.strip() else line for line in block.splitlines(True))+'\n\t}'
  return s[:start]+change+s[end:]
 old='\tuart_xmit_advance(uport, port->tx_remaining);\n\tgeni_se_tx_dma_unprep(&port->se, port->tx_dma_addr, port->tx_remaining);\n\tport->tx_dma_addr = 0;\n\tport->tx_remaining = 0;'
 new='\t/* Cancel/stop may have retired this transfer before a late DONE IRQ.\n\t * Do not unmap or advance ownership twice. DMA address zero is not\n\t * used as the discriminator: a nonzero byte count owns the mapping.\n\t */\n\tif (port->tx_remaining) {\n\t\tuart_xmit_advance(uport, port->tx_remaining);\n\t\tgeni_se_tx_dma_unprep(&port->se, port->tx_dma_addr, port->tx_remaining);\n\t\tport->tx_dma_addr = 0;\n\t\tport->tx_remaining = 0;\n\t}'
 if s.count(old)!=1:raise ValueError('Unknown GENI TX DMA completion source')
 return s.replace(old,new,1)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
 if a.output.exists() or a.source.resolve()==a.output.resolve():raise ValueError('Fresh output copy required')
 a.output.write_text(patch(a.source.read_text()))
