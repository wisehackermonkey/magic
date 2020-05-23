// ************************************************************************
//
// Copyright (c) 1995-2002 Juniper Networks, Inc. All rights reserved.
//
// Permission is hereby granted, without written agreement and without
// license or royalty fees, to use, copy, modify, and distribute this
// software and its documentation for any purpose, provided that the
// above copyright notice and the following three paragraphs appear in
// all copies of this software.
//
// IN NO EVENT SHALL JUNIPER NETWORKS, INC. BE LIABLE TO ANY PARTY FOR
// DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
// ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
// JUNIPER NETWORKS, INC. HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.
//
// JUNIPER NETWORKS, INC. SPECIFICALLY DISCLAIMS ANY WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
// NON-INFRINGEMENT.
//
// THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND JUNIPER
// NETWORKS, INC. HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT,
// UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
//
// ************************************************************************



/* ihash.c --
 *
 *  Implements "internal" hash, i.e. hash on user supplied structures,
 *  without need for parallel hash entry structs.
 *
 *  pointers involving users structs are cast to (void *)
 *  This should make the pointer arithmetic work out regardless
 *  of alignments within structs.
 *
 *  See ihash.h
 *
 */

#define DEREF(ptr,offset) ((ptr)+(offset))

static char rcsid[] = "$Header$";
#include <string.h>
#include <stdio.h>
#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/utils.h"
#include "utils/ihash.h"


/*
 * The following defines the ratio of # entries to # buckets
 * at which we rebuild the table to make it larger.
 */
static int iHashResizeRatio = 3;

/* forward reference */
static void iHashResize(IHashTable *table);

/* create a new hash table */
/* offsets should be generated by pointer subtraction of (void *) pointers */
extern IHashTable *IHashInit(
		      int nBuckets,
		      int keyOffset,
		      int nextOffset,
		      int (*hashFn)(void* key),
		      int (*sameKeyFn)(void* key1, void* key2)
		      )
{
  IHashTable *table;

  table = (IHashTable *)mallocMagic(sizeof(IHashTable));
  table->iht_table = (void **)callocMagic(sizeof(void *)*nBuckets);
  table->iht_nBucketsInit = nBuckets;
  table->iht_nBuckets = nBuckets;
  table->iht_nEntries = 0;
  table->iht_keyOffset = keyOffset;
  table->iht_nextOffset = nextOffset;
  table->iht_hashFn = hashFn;
  table->iht_sameKeyFn = sameKeyFn;

  return table;
}



/* free hash table (does not free client structures) */
void IHashFree(IHashTable *table)
{
  freeMagic((char *) table->iht_table);  /* free buckets */
  freeMagic((char *) table);
}

/* delete all entrys (and restore initial hash table size) */
void IHashClear(IHashTable *table)
{
  /* reinitial bucket array */
  freeMagic((char *) table->iht_table);
  table->iht_table = (void **)callocMagic(sizeof(void *)*table->iht_nBucketsInit);

  table->iht_nBuckets = table->iht_nBucketsInit;
  table->iht_nEntries = 0;
}

/* lookup an entry in table */
void *IHashLookUp(IHashTable *table, void *key)
{
  int hash;
  int bucket;
  void *entry;

  hash = (table->iht_hashFn)(key);
  bucket = ABS(hash) % table->iht_nBuckets;

  for(entry=table->iht_table[bucket];
      entry && !(table->iht_sameKeyFn)(key, DEREF(entry,(table->iht_keyOffset)));
      entry = *((void **) DEREF(entry,table->iht_nextOffset))) /* empty body*/;

  return entry;
}


/* return next matching entry */
void *IHashLookUpNext(IHashTable *table, void *prevEntry)
{
  void *entry;
  void *key = DEREF(prevEntry,table->iht_keyOffset);
  int hash = (table->iht_hashFn)(key);
  int bucket = ABS(hash) % table->iht_nBuckets;

  for(entry = *((void **) DEREF(prevEntry,table->iht_nextOffset));
      entry && !(table->iht_sameKeyFn)(key,DEREF(entry,table->iht_keyOffset));
      entry = *((void **) DEREF(entry,table->iht_nextOffset))) /* empty body*/;

  return entry;
}

/* add an entry in table */
void IHashAdd(IHashTable *table, void *entry)
{
  int hash;
  int bucket;

/*  ASSERT(!IHashLookUp(IHashTable, entry+IHashTable->iht_keyOffset),"IHashAdd"); */

  hash = (table->iht_hashFn)(DEREF(entry,table->iht_keyOffset));
  bucket = ABS(hash) % table->iht_nBuckets;

  *(void **)DEREF(entry,table->iht_nextOffset) = table->iht_table[bucket];
  table->iht_table[bucket] = entry;
  table->iht_nEntries++;

  /* if table getting crowded, resize it */
  if(table->iht_nEntries/table->iht_nBuckets >= iHashResizeRatio)
  {
    iHashResize(table);
  }
}

/* deletes an entry from table */
/*
 * NOTE: bplane code assumes IHashDelete() does not restructure table!
 * (IHashLookUpNext() is called before IHashDelete(), and then the enum.
 * is continued with further IHashLookUpNext() calls.
 */
void IHashDelete(IHashTable *table, void *entry)
{
  int hash;
  int bucket;
  void **pp;
  int nextOffset = table->iht_nextOffset;

  hash = (table->iht_hashFn)DEREF(entry,table->iht_keyOffset);
  bucket = ABS(hash) % table->iht_nBuckets;

  for(pp = &table->iht_table[bucket];
      (*pp) && (*pp) != entry;
      pp = DEREF((*pp),nextOffset));

  ASSERT(*pp,"IHashDelete");
  (*pp) = * (void **) DEREF(entry,nextOffset);
  table->iht_nEntries--;
}

/* return number of entries in table */
int IHashEntries(IHashTable *table)
{
  return table->iht_nEntries;
}

/* call back client func on each entry in hashtable */
void IHashEnum(IHashTable *table, void (*clientFunc)(void *entry))
{
  int bucket;

  for(bucket=0; bucket<table->iht_nBuckets; bucket++)
  {
    void *entry;
    for(entry=table->iht_table[bucket];
	entry;
        entry = *((void **) DEREF(entry,table->iht_nextOffset)))
    {
      (*clientFunc)(entry);
    }
  }
}

/* grow hash table */
static void iHashResize(IHashTable *table)
{
  void **oldBuckets = table->iht_table;
  int oldSize = table->iht_nBuckets;
  int newSize = oldSize*4;
  int bucket;

  /* alloc a new table */
  table->iht_table = (void **)callocMagic(sizeof(void *)*newSize);
  table->iht_nBuckets = newSize;
  table->iht_nEntries = 0;

  /* add back all entries in old table */
  for(bucket=0; bucket<oldSize; bucket++)
  {
    void *entry;
    void *next;
    for(entry=oldBuckets[bucket];
	entry;
        entry = next)
    {
      next = *((void **) DEREF(entry,table->iht_nextOffset));
      IHashAdd(table,entry);
    }
  }

  /* finally, free old table */
  freeMagic((char *) oldBuckets);
}

/* print out statistics */
void IHashStats(IHashTable *table)
{
  int bucket;

  fprintf(stderr,"Internal Hash Statistics:\n");
  fprintf(stderr,"\tinitial buckets = %d\n", table->iht_nBucketsInit);
  fprintf(stderr,"\tbuckets = %d\n", table->iht_nBuckets);
  fprintf(stderr,"\tentries = %d\n", table->iht_nEntries);
  fprintf(stderr,"\tkey offset = %d\n", table->iht_keyOffset);
  fprintf(stderr,"\tnext offset = %d\n", table->iht_nextOffset);
  fprintf(stderr,"\ndistribution:  ");

  for(bucket=0; bucket<table->iht_nBuckets; bucket++)
  {
    void *entry;
    int num = 0;
    for(entry=table->iht_table[bucket];
	entry;
        entry = *((void **) DEREF(entry,table->iht_nextOffset)))
    {
      num++;
    }
    fprintf(stderr,"%d ",num);
  }
}

/* return statistics on hash table  (returns memory utilized by table) */
int IHashStats2(IHashTable *table,
		int *nBuckets,      /* if non-null return num buckets here */
		int *nEntries)      /* if non-null return num entries here */
{

  if(nBuckets) *nBuckets = table->iht_nBuckets;
  if(nEntries) *nEntries = table->iht_nEntries;

  return
    IHashAlignedSize(sizeof(IHashTable)) +
    IHashAlignedSize(sizeof(void *)* table->iht_nBuckets);
}

/* hash for key fields that are pointers to strings */
int IHashStringPKeyHash(void *key)
{
  char *s = * (char **) key;
  int i=0;

  /* Add up the characters as though this were a number */
  while (*s != 0) i = (i*10) + (*s++ - '0');
  if(i<0) i = -i;

  return i;
}

/* compare string pointer keys */
int IHashStringPKeyEq(void *key1, void *key2)
{
  return strcmp(* (char **) key1, * (char **) key2)==0;
}

/* hash for key fields that are strings */
int IHashStringKeyHash(void *key)
{
  char *s =  (char *) key;
  int i=0;

  /* Add up the characters as though this were a number */
  while (*s != 0) i = (i*10) + (*s++ - '0');
  if(i<0) i = -i;

  return i;
}

/* compare string keys */
int IHashStringKeyEq(void *key1, void *key2)
{
  return strcmp((char *) key1, (char *) key2)==0;
}

/* hash for key fields that are 32 bit words */
int IHashWordKeyHash(void *keyp)
{
  /* just use the value of word itself! */
  return * (int *) keyp;
}

/* compare struc for 32 bit word keys */
int IHashWordKeyEq(void *key1p, void *key2p)
{
  return ( *(int *) key1p == * (int *) key2p);
}


/* hash for n-byte field */
static __inline__ int
iHash(void *key, int n)
{
  char *s =  (char *) key;
  int hash=0;
  int i;

  /* Add up the characters as though this were a number */
  for(i=0;i<n;i++) hash = (hash*10) + (s[i] - '0');
  if(hash<0) hash = -hash;

  return hash;
}

/* hash for key fields that are four words long */
int IHash4WordKeyHash(void *keyp)
{
  return iHash(keyp,4*sizeof(int));
}

/* compare struc for 4 word keys */
int IHash4WordKeyEq(void *key1p, void *key2p)
{
  return (memcmp(key1p,key2p,4*sizeof(int)) == 0);
}
