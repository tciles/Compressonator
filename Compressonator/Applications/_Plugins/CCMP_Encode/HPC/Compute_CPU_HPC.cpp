//=====================================================================
// Copyright (c) 2019    Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
/// \file Compute_CPU_HPC.cpp
//
//=====================================================================

#include "PluginInterface.h"
#include "CCPU_HPC.h"

#ifdef _WIN32
#include "query_timer.h"
#else
#define _stdcall
#endif

//#include "debug.h"

#ifdef _LOCAL_DEBUG
char    DbgTracer::buff[MAX_DBGBUFF_SIZE];
char    DbgTracer::PrintBuff[MAX_DBGPPRINTBUFF_SIZE];
#endif

unsigned int    _stdcall ProcEncode(void* param)
{
   ThreadParam *tp = (ThreadParam*)param;

   tp->run = false;
   //printf("Thead Active [%4x]\n",std::this_thread::get_id());

   while (tp->exit == false)
   {
       if (tp->run == true)
       {
           tp->cmp_encoder->CompressBlock(tp->x,tp->y,tp->in, tp->out);
           tp->run = false;
       }

        using namespace chrono;
        std::this_thread::sleep_for(0ms);
   }

   // printf("Thead Closed [%x] run[%d]\n",std::this_thread::get_id(),tp->run?1:0);
   return 0;
}

CodecError CCPU_HPC::CreateEncoderThreadPool()
{
   if (!m_ThreadCodecInitialized)
   {
    //printf("m_ThreadCodecInitialized %d\n",m_NumEncodingThreads);
      // Create the encoding threads in the suspended state
      for (int i = 0; i < m_NumEncodingThreads; i++)
      {

            m_encoder[i] = (CMP_Encoder*)m_plugin_compute->TC_Create();

            // Cleanup if problem!
            if (!m_encoder[i])
            {
                for (int j = 0; j < i; j++) {
                    m_plugin_compute->TC_Destroy(m_encoder[j]);
                }
                return CE_Unknown;
            }

         m_encoder[i]->m_quality    = m_SourceInfo.m_fquality;
         m_encoder[i]->m_srcHeight  = m_SourceInfo.m_src_height;
         m_encoder[i]->m_srcWidth   = m_SourceInfo.m_src_width;
         m_encoder[i]->m_xdim       = m_SourceInfo.m_width_in_blocks;
         m_encoder[i]->m_ydim       = m_SourceInfo.m_height_in_blocks;
         m_encoder[i]->m_zdim       = 0;

         m_EncodeParameterStorage[i].cmp_encoder = m_encoder[i];
         m_EncodeParameterStorage[i].run  = false;
         m_EncodeParameterStorage[i].exit = false;
         m_EncodeParameterStorage[i].in   = NULL;
         m_EncodeParameterStorage[i].out  = NULL;
         m_EncodeParameterStorage[i].x    = 0;
         m_EncodeParameterStorage[i].y    = 0;

         m_EncodingThreadHandle[i] = std::thread(ProcEncode, (void*)&m_EncodeParameterStorage[i]);
      }

      m_ThreadCodecInitialized = true;
   }

    return CE_OK;
}

void CCPU_HPC::DeleteEncoderThreadPool()
{
    // Make sure threads are not in running any state
   // Wait for all the live threads to finish any current work
    if (m_ThreadCodecInitialized)
    {
        // Wait for Threads to exit
        for (CMP_DWORD dwThread = 0; dwThread < m_NumEncodingThreads; dwThread++)
        {
              m_EncodeParameterStorage[dwThread].exit = true;
              std::thread& curThread = m_EncodingThreadHandle[dwThread];
              curThread.join();
         }

        // Clean up data storage
        for (unsigned int i = 0; i < m_NumEncodingThreads; i++)
        {
            if(m_encoder[i])
                m_plugin_compute->TC_Destroy(m_encoder[i]);

         m_EncodeParameterStorage[i].cmp_encoder = NULL;

         m_EncodeParameterStorage[i].run  = false;
         m_EncodeParameterStorage[i].exit = false;
         m_EncodeParameterStorage[i].in   = NULL;
         m_EncodeParameterStorage[i].out  = NULL;
         m_EncodeParameterStorage[i].x    = 0;
         m_EncodeParameterStorage[i].y    = 0;
        }

        m_ThreadCodecInitialized = false;
    }
}

CodecError CCPU_HPC::EncodeThreadBlock(int x, int y,  void *in,    void *out)
{
if (m_Use_MultiThreading)
{
    CMP_WORD   threadIndex;

    // Loop and look for an available thread
    CMP_BOOL found = false;
    threadIndex = 0;
    // printf("threadIndex = %d\n",threadIndex);
    while (found == false)
    {
        if (m_EncodeParameterStorage[threadIndex].run == false)
        {
            found = true;
            break;
        }

        // Increment and wrap the thread index
        threadIndex++;
        if (threadIndex == m_NumEncodingThreads)
        {
            threadIndex = 0;
        }
    }

    m_EncodeParameterStorage[threadIndex].x = x;
    m_EncodeParameterStorage[threadIndex].y = y;

    // point to the input data 
    m_EncodeParameterStorage[threadIndex].in = in;

    // Set the output pointer for the thread to the provided location
    m_EncodeParameterStorage[threadIndex].out = out;

    // Tell the thread to start working
    m_EncodeParameterStorage[threadIndex].run = true;
}
else
{
//printf("HPC Use single threaded\n");
    // Copy the input data into the thread storage
    // point to the input data 
    m_EncodeParameterStorage[0].in = in;

    // Set the output pointer for the thread to the provided location
    m_EncodeParameterStorage[0].out = out;

    m_EncodeParameterStorage[0].x = x;
    m_EncodeParameterStorage[0].y = y;

    m_encoder[0]->CompressBlock(x,y,in,out);
}
    return CE_OK;
}

void CCPU_HPC::FinishThreadEncoding()
{
    // Wait for all the live threads to finish any current work
    for (CMP_DWORD i = 0; i < m_NumEncodingThreads; i++)
    {
       using namespace chrono;
       // If a thread is in the running state then we need to wait for it to finish
       // its work from the producer
       while (m_EncodeParameterStorage[i].run == true)
       {
           std::this_thread::sleep_for(1ms);
       }

       m_EncodeParameterStorage[i].exit = true;
    }

}

void CCPU_HPC::Init()
{
#ifdef _WIN32
    query_timer::initialize();
#endif
    m_plugin_compute            = NULL;
    m_ThreadCodecInitialized    = false;
    m_current_format            = CMP_FORMAT_Unknown;

//printf("HPC Threads input %d\n",m_kernel_options->threads);
    if (m_kernel_options->threads != 1)
    {
        m_NumEncodingThreads        = m_kernel_options->threads;
        if (m_NumEncodingThreads == 0)
        {
            m_NumEncodingThreads = CMP_NumberOfProcessors();
            if (m_NumEncodingThreads <= 2)
                m_NumEncodingThreads = 8; // fallback to a default!
            if (m_NumEncodingThreads > 128)
                m_NumEncodingThreads = 128;

        }
        m_Use_MultiThreading       = true;  //always enable multithread by default for this release!
    }
    else
    {
        m_NumEncodingThreads        = 1;
        m_Use_MultiThreading        = false;
    }

    //printf("HPC Threads set %d\n",m_kernel_options->threads);

    m_EncodeParameterStorage = new ThreadParam[m_NumEncodingThreads];
    m_EncodingThreadHandle   = new std::thread[m_NumEncodingThreads];
    if (!m_EncodingThreadHandle)
    {
       delete[] m_EncodeParameterStorage;
       m_EncodeParameterStorage = NULL;
    }

    for (int i=0; i<m_NumEncodingThreads; i++)
    {
        m_encoder[i] = NULL;
    }

}

CCPU_HPC::CCPU_HPC(void *ukernelOptions)
{
    m_kernel_options = (KernelOptions *)ukernelOptions;
    Init();
}

void CCPU_HPC::SetComputeOptions(ComputeOptions *CLOptions)
{

    // Hold ref to the compute codec plugin for use in Compute Framework
    m_plugin_compute = reinterpret_cast<PluginInterface_Encoder *>(CLOptions->plugin_compute);
 
}

CCPU_HPC::~CCPU_HPC()
{
//   if (m_EncodeParameterStorage)
//           delete[] m_EncodeParameterStorage;
//   if (m_EncodingThreadHandle)
//           delete[] m_EncodingThreadHandle;;
}


#include "cmp_hpc.h"

//extern void CompressImage(const texture_surface* input, BYTE* output);
extern void InitWin32Threads(int numThreads) ;
extern void DestroyThreads();
extern void Initialize();

CMP_Encoder* g_plugin_compute;

void CompressTexture(const texture_surface* input, unsigned char* output)
{
    g_plugin_compute->CompressTexture((void *)input, (void *)output, nullptr);
}

CMP_ERROR CCPU_HPC::Compress(KernelOptions *Options, MipSet  &SrcTexture, MipSet  &destTexture,CMP_Feedback_Proc pFeedback)
{
    if (m_plugin_compute == NULL) return(CMP_ERR_UNABLE_TO_INIT_COMPUTELIB);


#ifdef USE_GTC
    if (destTexture.m_format == CMP_FORMAT_GTC)
    {
        if (Options->fquality == 0.0f)
        {
            CMP_Encoder*  encoder = (CMP_Encoder*)m_plugin_compute->TC_Create();
            if (encoder)
            {
                int res = encoder->CompressTexture(&SrcTexture,&destTexture, nullptr);
                if (res != 0)
                {
                    printf("failed");
                }
                m_plugin_compute->TC_Destroy(encoder);
                return(CMP_OK);
            }
            return(CMP_ERR_GENERIC);
        }
    }
#endif

#ifdef USE_ASPM_CODE
    // Prototype Code: Enabling use of SPMD (enabled only on Windows Platforms)
    if (destTexture.m_format == CMP_FORMAT_BC7)
    {
        // When ASPM code path is enabled
        if (Options->fquality == 0.99f)
        {
            g_plugin_compute = (CMP_Encoder*)m_plugin_compute->TC_Create();
            Initialize();
            InitWin32Threads(Options->threads);
            texture_surface edged_img;
            edged_img.height = SrcTexture.dwHeight;
            edged_img.width  = SrcTexture.dwWidth;
            edged_img.stride = SrcTexture.dwWidth * 4;
            edged_img.ptr    = SrcTexture.pData;
            m_plugin_compute->TC_Start();
            CompressSTMT(&edged_img,destTexture.pData,Options->threads);
            m_plugin_compute->TC_End();
            DestroyThreads();
            m_plugin_compute->TC_Destroy(g_plugin_compute);
            return(CMP_OK);
        }
    }
#endif

    m_source_file                = Options->srcfile;

    // Update addtion kernel option settings
    m_kernel_options->data       = Options->data;
    m_kernel_options->size       = Options->size;
    m_kernel_options->format     = Options->format;
    m_kernel_options->dataSVM    = Options->dataSVM;
    m_source_buffer_size         = SrcTexture.dwDataSize;
    p_destination                = destTexture.pData;
    m_destination_size           = destTexture.dwDataSize;

    CMP_UINT m_padded_width_in_blocks;
    CMP_UINT m_padded_height_in_blocks;

    m_current_format = destTexture.m_format;

    if (destTexture.m_nBlockWidth > 0)
    {
        m_width_in_blocks = (CMP_UINT)SrcTexture.dwWidth / destTexture.m_nBlockWidth;
        m_padded_width_in_blocks = (((CMP_UINT)SrcTexture.dwWidth + 3) >> 2);
    }
    else
    {
        return CMP_ERR_GENERIC;
    }

    if (destTexture.m_nBlockHeight > 0)
    {
        m_height_in_blocks = (CMP_UINT)SrcTexture.dwHeight / destTexture.m_nBlockHeight;
        m_padded_height_in_blocks = (((CMP_UINT)SrcTexture.dwHeight + 3) >> 2);
    }        
    else
    {
        return CMP_ERR_GENERIC;
    }

    m_psource = (CMP_Vec4uc *)SrcTexture.pData;

    m_SourceInfo.m_src_height       = SrcTexture.dwHeight;
    m_SourceInfo.m_src_width        = SrcTexture.dwWidth;
    m_SourceInfo.m_width_in_blocks  = m_width_in_blocks;
    m_SourceInfo.m_height_in_blocks = m_height_in_blocks;
    m_SourceInfo.m_fquality         = Options->fquality;

    {

      // initialize the Encoder based on num thread set in Init
        CreateEncoderThreadPool();

        // printf("Encoder %x Source %x  Destination %x\n",m_plugin_compute, m_psource,p_destination);
        CMP_FLOAT xyblocks = (CMP_FLOAT)m_padded_height_in_blocks * m_padded_width_in_blocks;

        if (xyblocks <= 0.01f) xyblocks = 1.0;

        unsigned int y = 0; 
        unsigned int x;
        float progress;
        float progress_old = FLT_MAX;
        CMP_INT lineAtPercent = (CMP_INT)(m_padded_height_in_blocks * 0.01f);
        if (lineAtPercent <= 0)  lineAtPercent = 1;

        m_plugin_compute->TC_Start();

        while (y < m_padded_height_in_blocks)
        {
            for (x = 0; x < m_padded_width_in_blocks; x++)
            {
               EncodeThreadBlock(x, y, (void *)m_psource, (void *)p_destination);
            }
            y++;
            if (pFeedback)
            {
                if (((y % lineAtPercent) == 0) && (xyblocks > 0))
                {
                    progress = (x*y)/xyblocks;
                    if (progress_old != progress)
                    {
                        progress_old = progress;
                        if (pFeedback(progress * 100.0f,NULL,NULL))
                        {
                            break;
                        }
                    }
                }
            }
        }

        // Wait for all threads to Finish
        FinishThreadEncoding();

        // Delete the Encoder Thread Pool
        DeleteEncoderThreadPool();

        m_plugin_compute->TC_End();
    }

    return(CMP_OK);
}


