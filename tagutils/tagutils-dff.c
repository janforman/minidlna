//=========================================================================
// FILENAME     : tagutils-dff.c
// DESCRIPTION  : DFF metadata reader
//=========================================================================
// Copyright (c) 2014 Takeshich NAKAMURA
//=========================================================================

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#define GET_DFF_INT64(p) ((((uint64_t)((p)[0])) << 56) |   \
			  (((uint64_t)((p)[1])) << 48) |   \
			  (((uint64_t)((p)[2])) << 40) |   \
			  (((uint64_t)((p)[3])) << 32) |   \
			  (((uint64_t)((p)[4])) << 24) |   \
			  (((uint64_t)((p)[5])) << 16) |   \
			  (((uint64_t)((p)[6])) << 8) |    \
			  (((uint64_t)((p)[7]))))

#define GET_DFF_INT32(p) ((((uint32_t)((p)[0])) << 24) |   \
			  (((uint32_t)((p)[1])) << 16) |   \
			  (((uint32_t)((p)[2])) << 8) |     \
			  (((uint32_t)((p)[3]))))

#define GET_DFF_INT16(p) ((((uint16_t)((p)[0])) << 8) |   \
			  (((uint16_t)((p)[1]))))

/* Upper bound for the PROP and DIIN chunks, which are read into memory
 * as a whole.  Real-world chunks are a few hundred bytes. */
#define DFF_MAX_CHUNK_SIZE (1024 * 1024)

/* Skip over chunk data.  A size that does not fit in off_t would turn
 * into a backwards seek and make the parser loop forever. */
static int
_dff_skip(FILE *fp, uint64_t size)
{
	uint64_t max = sizeof(off_t) == 8 ? (uint64_t)INT64_MAX : (uint64_t)INT32_MAX;

	if (size > max)
		return -1;
	return fseeko(fp, (off_t)size, SEEK_CUR);
}

static int
_get_dfffileinfo(char *file, struct song_metadata *psong)
{
	FILE *fp;
	uint32_t len;
	uint32_t rt;
	unsigned char hdr[32] = { 0 };

	uint64_t totalsize = 0;
	uint64_t propckDataSize = 0;
	uint64_t count = 0;
	uint32_t samplerate = 0;
	uint16_t channels = 0;
	//DST
	uint64_t dstickDataSize = 0;
	uint32_t numFrames = 0;
	uint16_t frameRate = 0;
	unsigned char frteckData[18] = { 0 };
	unsigned char dstickData[12] = { 0 };
	uint64_t totalcount = 0;
	unsigned char ckbuf[12] = { 0 };
	unsigned char compressionType[4] = { 0 };
	unsigned char dsdsdckData[12] = { 0 };
	uint64_t dsdsdckDataSize = 0;
	uint64_t comtckDataSize = 0;
	uint64_t diinckDataSize = 0;
	uint64_t manfckDataSize = 0;

	//DPRINTF(E_DEBUG,L_SCANNER,"Getting DFF fileinfo =%s\n",file);

	if ((fp = fopen(file, "rb")) == NULL)
	{
		DPRINTF(E_WARN, L_SCANNER, "Could not create file handle\n");
		return -1;
	}

	len = 32;
	//Form DSD chunk
	if (!(rt = fread(hdr, len, 1, fp)))
	{
		DPRINTF(E_WARN, L_SCANNER, "Could not read Form DSD chunk from %s\n", file);
		fclose(fp);
		return -1;
	}

	if (strncmp((char*)hdr, "FRM8", 4))
	{
		DPRINTF(E_WARN, L_SCANNER, "Invalid Form DSD chunk in %s\n", file);
		fclose(fp);
		return -1;
	}

	totalsize = GET_DFF_INT64(hdr + 4);

	if (strncmp((char*)hdr + 12, "DSD ", 4))
	{
		DPRINTF(E_WARN, L_SCANNER, "Invalid Form DSD chunk in %s\n", file);
		fclose(fp);
		return -1;
	}

	//FVER chunk
	if (strncmp((char*)hdr + 16, "FVER", 4))
	{
		DPRINTF(E_WARN, L_SCANNER, "Invalid Format Version Chunk in %s\n", file);
		fclose(fp);
		return -1;
	}

	totalsize -= 16;
	while (totalcount < totalsize - 4)
	{
		if (!(rt = fread(ckbuf, sizeof(ckbuf), 1, fp)))
		{
			//DPRINTF(E_WARN, L_SCANNER, "Could not read chunk header from %s\n", file);
			//fclose(fp);
			//return -1;
			break;
		}

		//Property chunk
		if (strncmp((char*)ckbuf, "PROP", 4) == 0)
		{
			unsigned char *propckData;

			propckDataSize = GET_DFF_INT64(ckbuf + 4);
			if (propckDataSize < 4 || propckDataSize > DFF_MAX_CHUNK_SIZE)
			{
				DPRINTF(E_WARN, L_SCANNER, "Invalid Property chunk size in %s\n", file);
				fclose(fp);
				return -1;
			}
			totalcount += propckDataSize + 12;

			propckData = malloc(propckDataSize);
			if (!propckData)
			{
				fclose(fp);
				return -1;
			}

			if (!(rt = fread(propckData, propckDataSize, 1, fp)))
			{
				DPRINTF(E_WARN, L_SCANNER, "Could not read Property chunk from %s\n", file);
				free(propckData);
				fclose(fp);
				return -1;
			}

			if (strncmp((char*)propckData, "SND ", 4))
			{
				DPRINTF(E_WARN, L_SCANNER, "Invalid Property chunk in %s\n", file);
				free(propckData);
				fclose(fp);
				return -1;
			}

			/* Each sub-chunk is: 4-byte ID, 8-byte size, data.
			 * Make sure the header and the data fit in the chunk. */
			count = 4;
			while (count + 12 <= propckDataSize)
			{
				uint64_t subsize = GET_DFF_INT64(propckData + count + 4);
				unsigned char *subdata = propckData + count + 12;

				if (subsize > propckDataSize - count - 12)
					break;

				if (strncmp((char*)propckData + count, "FS  ", 4) == 0)
				{
					//Sample Rate Chunk
					if (subsize < 4)
						break;
					samplerate = GET_DFF_INT32(subdata);
					psong->samplerate = samplerate;

					//DEBUG DPRINTF(E_DEBUG, L_SCANNER, "Sample Rate is %d\n", psong->samplerate);
				}
				else if (strncmp((char*)propckData + count, "CHNL", 4) == 0)
				{
					//Channels Chunk
					if (subsize < 2)
						break;
					channels = GET_DFF_INT16(subdata);
					psong->channels = channels;

					//DEBUG DPRINTF(E_DEBUG, L_SCANNER, "channels is %d\n", channels);
				}
				else if (strncmp((char*)propckData + count, "CMPR", 4) == 0)
				{
					//Compression Type Chunk
					if (subsize < 4)
						break;
					memcpy(compressionType, subdata, 4);
				}
				else if (strncmp((char*)propckData + count, "ABSS", 4) == 0)
				{
					//Absolute Start Time Chunk
				}
				else if (strncmp((char*)propckData + count, "LSCO", 4) == 0)
				{
					//Loudsperaker Configuration Chunk
				}
				else
				{
					break;
				}
				count += 12 + subsize;
			}
			free(propckData);

			//bitrate bitpersample is 1bit
			psong->bitrate = channels * samplerate * 1;

			//DSD/DST Sound Data Chunk
			len = 12;
			if (!(rt = fread(dsdsdckData, len, 1, fp)))
			{
				DPRINTF(E_WARN, L_SCANNER, "Could not read DSD/DST Sound Data chunk from %s\n", file);
				fclose(fp);
				return -1;
			}

			if (strncmp((char*)compressionType, (char*)dsdsdckData, 4))
			{
				DPRINTF(E_WARN, L_SCANNER, "Invalid DSD/DST Sound Data chunk in %s\n", file);
				fclose(fp);
				return -1;
			}

			if (strncmp((char*)dsdsdckData, "DSD ", 4) == 0)
			{
				//DSD
				dsdsdckDataSize = GET_DFF_INT64(dsdsdckData + 4);
				totalcount += dsdsdckDataSize + 12;
				if (samplerate && channels)
					psong->song_length = (int)((double)dsdsdckDataSize / (double)samplerate / (double)channels * 8 * 1000);

				//DEBUG DPRINTF(E_DEBUG, L_SCANNER, "songlength is %d\n", psong->song_length);

				if (_dff_skip(fp, dsdsdckDataSize))
					break;
			}
			else if (strncmp((char*)dsdsdckData, "DST ", 4) == 0)
			{
				//DST
				dsdsdckDataSize = GET_DFF_INT64(dsdsdckData + 4);
				totalcount += dsdsdckDataSize + 12;

				//DST Frame Information chunk
				if (!(rt = fread(frteckData, 18, 1, fp)))
				{
					DPRINTF(E_WARN, L_SCANNER, "Could not read DST Frame Information chunk from %s\n", file);
					fclose(fp);
					return -1;
				}

				if (strncmp((char*)frteckData, "FRTE", 4) == 0)
				{
					//uint64_t frteckDataSize = GET_DFF_INT64(frteckData+4);
					numFrames = GET_DFF_INT32(frteckData + 12);
					frameRate = GET_DFF_INT16(frteckData + 16);

					if (frameRate)
						psong->song_length = numFrames / frameRate * 1000;

					if (dsdsdckDataSize < 18 || _dff_skip(fp, dsdsdckDataSize - 18))
						break;
				}
				else
				{
					DPRINTF(E_WARN, L_SCANNER, "Invalid DST Frame Information chunk in %s\n", file);
					fclose(fp);
					return -1;
				}

				//DST Sound Index Chunk
				if (!(rt = fread(dstickData, 12, 1, fp)))
				{
					if (ferror(fp))
					{
						DPRINTF(E_WARN, L_SCANNER, "Could not read DST Sound Index chunk from %s\n", file);
						fclose(fp);
						return -1;
					}
					else
					{
						//EOF
						break;
					}
				}

				if (strncmp((char*)dstickData, "DSTI", 4) == 0)
				{
					dstickDataSize = GET_DFF_INT64(dstickData + 4);
					totalcount += dstickDataSize + 12;
					if (_dff_skip(fp, dstickDataSize))
						break;
				}
				else
				{
					fseeko(fp, -12, SEEK_CUR);
				}
			}
			else
			{
				DPRINTF(E_WARN, L_SCANNER, "Invalid DSD/DST Sound Data chunk in %s\n", file);
				fclose(fp);
				return -1;
			}
		}
		else if (!strncmp((char*)ckbuf, "COMT", 4))
		{
			//COMT Chunk
			comtckDataSize = GET_DFF_INT64(ckbuf + 4);
			totalcount += comtckDataSize + 12;
			if (_dff_skip(fp, comtckDataSize))
				break;
		}
		else if (!strncmp((char*)ckbuf, "DIIN", 4))
		{
			//Edited Master Information chunk
			unsigned char *diinckData;

			diinckDataSize = GET_DFF_INT64(ckbuf + 4);
			totalcount += diinckDataSize + 12;
			if (diinckDataSize == 0 || diinckDataSize > DFF_MAX_CHUNK_SIZE)
			{
				/* Optional metadata: skip it, keep the audio info */
				DPRINTF(E_WARN, L_SCANNER, "Skipping Edited Master Information chunk of invalid size in %s\n", file);
				if (_dff_skip(fp, diinckDataSize))
					break;
				continue;
			}

			diinckData = malloc(diinckDataSize);
			if (!diinckData)
			{
				fclose(fp);
				return -1;
			}

			if (!(rt = fread(diinckData, diinckDataSize, 1, fp)))
			{
				DPRINTF(E_WARN, L_SCANNER, "Could not read Edited Master Information chunk from %s\n", file);
				free(diinckData);
				fclose(fp);
				return -1;
			}

			/* Each sub-chunk is: 4-byte ID, 8-byte size, data.
			 * Make sure the header and the data fit in the chunk. */
			uint64_t icount = 0;
			while (icount + 12 <= diinckDataSize)
			{
				uint64_t subsize = GET_DFF_INT64(diinckData + icount + 4);
				unsigned char *subdata = diinckData + icount + 12;

				if (subsize > diinckDataSize - icount - 12)
					break;

				if (!strncmp((char*)diinckData + icount, "EMID", 4))
				{
					//Edited Master ID chunk
				}
				else if (!strncmp((char*)diinckData + icount, "MARK", 4))
				{
					//Master Chunk
				}
				else if (!strncmp((char*)diinckData + icount, "DIAR", 4))
				{
					//Artist Chunk: 4-byte text length, then the text
					if (subsize < 4)
						break;
					if (!psong->contributor[ROLE_ARTIST])
						psong->contributor[ROLE_ARTIST] = strndup((char*)subdata + 4, subsize - 4);
				}
				else if (!strncmp((char*)diinckData + icount, "DITI", 4))
				{
					//Title Chunk: 4-byte text length, then the text
					if (subsize < 4)
						break;
					if (!psong->title)
						psong->title = strndup((char*)subdata + 4, subsize - 4);
				}
				else
				{
					break;
				}
				icount += 12 + subsize;
			}
			free(diinckData);
		}
		else if (!strncmp((char*)ckbuf, "MANF", 4))
		{
			//Manufacturer Specific Chunk
			manfckDataSize = GET_DFF_INT64(ckbuf + 4);
			totalcount += manfckDataSize + 12;
			if (_dff_skip(fp, manfckDataSize))
				break;
		}
	}

	fclose(fp);

	//DPRINTF(E_DEBUG, L_SCANNER, "totalsize is 0x%016lx\n", (long unsigned int)totalsize);
	//DPRINTF(E_DEBUG, L_SCANNER, "propckDataSize is 0x%016lx\n", (long unsigned int)propckDataSize);
	//DPRINTF(E_DEBUG, L_SCANNER, "dsdsdckDataSize is 0x%016lx\n", (long unsigned int)dsdsdckDataSize);
	//DPRINTF(E_DEBUG, L_SCANNER, "dstickDataSize is 0x%016lx\n", (long unsigned int)dstickDataSize);
	//DPRINTF(E_DEBUG, L_SCANNER, "comtckDataSize is 0x%016lx\n", (long unsigned int)comtckDataSize);
	//DPRINTF(E_DEBUG, L_SCANNER, "diinckDataSize is 0x%016lx\n", (long unsigned int)diinckDataSize);
	//DPRINTF(E_DEBUG, L_SCANNER, "manfckDataSize is 0x%016lx\n", (long unsigned int)manfckDataSize);


	//DPRINTF(E_DEBUG, L_SCANNER, "Got dff fileinfo successfully=%s\n", file);
	//DEBUG DPRINTF(E_DEBUG, L_SCANNER, "TITLE is %s\n",psong->title );
	//DEBUG DPRINTF(E_DEBUG, L_SCANNER, "ARTIST is %s\n",psong->contributor[ROLE_ARTIST]);
	//DEBUG DPRINTF(E_DEBUG, L_SCANNER, "samplerate is  %d\n", psong->samplerate);
	//DEBUG DPRINTF(E_DEBUG, L_SCANNER, "song_length is  %d\n", psong->song_length);
	//DEBUG DPRINTF(E_DEBUG, L_SCANNER, "channels are  %d\n", psong->channels);
	//DEBUG DPRINTF(E_DEBUG, L_SCANNER, "bitrate is  %d\n", psong->bitrate);

	xasprintf(&(psong->dlna_pn), "DFF");
	return 0;
}
