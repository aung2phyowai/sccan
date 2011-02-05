#include "antsCommandLineOption.h"
#include "antsCommandLineParser.h"
#include "itkImage.h"
#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkImageRegionIteratorWithIndex.h"
#include "itkRecursiveGaussianImageFilter.h"
#include "itkDiscreteGaussianImageFilter.h"
#include "itkResampleImageFilter.h"
#include "itkBSplineInterpolateImageFunction.h"
#include <string>
#include <algorithm>
#include <vector>
#include <vnl/vnl_random.h>
#include <vnl/algo/vnl_qr.h>
#include <vnl/algo/vnl_svd.h>
#include <vnl/algo/vnl_svd_economy.h>
#include <vnl/algo/vnl_symmetric_eigensystem.h>
#include <vnl/algo/vnl_real_eigensystem.h>
#include <vnl/algo/vnl_generalized_eigensystem.h>
#include "antsSCCANObject.h" 

template <class TComp>
double vnl_pearson_corr( vnl_vector<TComp> v1, vnl_vector<TComp> v2 )
{
  double xysum=0;
  for ( unsigned int i=0; i<v1.size(); i++) xysum+=v1(i)*v2(i);
  double frac=1.0/(double)v1.size();
  double xsum=v1.sum(),ysum=v2.sum();
  double xsqr=v1.squared_magnitude();
  double ysqr=v2.squared_magnitude();
  double numer=xysum - frac*xsum*ysum;
  double denom=sqrt( ( xsqr - frac*xsum*xsum)*( ysqr - frac*ysum*ysum) );
  if ( denom <= 0 ) return 0;
  return numer/denom;
}

template <class TImage,class TComp>
void WriteVectorToSpatialImage( std::string filename , std::string post , vnl_vector<TComp> w_p , typename TImage::Pointer  mask )
{
      typedef itk::Image<TComp,2> MatrixImageType;
      typedef typename TImage::PixelType PixelType;
      std::string::size_type pos = filename.rfind( "." );
      std::string filepre = std::string( filename, 0, pos );
      std::string extension;
      if ( pos != std::string::npos ){
        extension = std::string( filename, pos, filename.length()-1);
        if (extension==std::string(".gz")){
	  pos = filepre.rfind( "." );
	  extension = std::string( filepre, pos, filepre.length()-1 )+extension;
          filepre = std::string( filepre, 0, pos );
        }
      }
      
      typename TImage::Pointer weights = TImage::New();
      weights->SetOrigin( mask->GetOrigin() );
      weights->SetSpacing( mask->GetSpacing() );
      weights->SetRegions( mask->GetLargestPossibleRegion() );
      weights->SetDirection( mask->GetDirection() );
      weights->Allocate();
      weights->FillBuffer( itk::NumericTraits<PixelType>::Zero );

      // overwrite weights with vector values;
      unsigned long vecind=0;
      typedef itk::ImageRegionIteratorWithIndex<TImage> Iterator;
      Iterator mIter(mask,mask->GetLargestPossibleRegion() );
      for(  mIter.GoToBegin(); !mIter.IsAtEnd(); ++mIter )
	if (mIter.Get() >= 0.5) 
	  {
	    TComp val=w_p(vecind);
	    weights->SetPixel(mIter.GetIndex(),val);
	    vecind++;
	  } 
	else mIter.Set(0);

      typedef itk::ImageFileWriter<TImage> WriterType;
      std::string fn1=filepre+post+extension;
      std::cout << fn1 << std::endl;
      typename WriterType::Pointer writer = WriterType::New();
      writer->SetFileName( fn1 );
      writer->SetInput( weights );
      writer->Update();     

}

template <class TImage,class TComp>
vnl_matrix<TComp> 
CopyImageToVnlMatrix( typename TImage::Pointer   p_img )
{
  typedef vnl_matrix<TComp> vMatrix;
  
  typename TImage::SizeType  pMatSize=p_img->GetLargestPossibleRegion().GetSize();
  vMatrix p(pMatSize[0],pMatSize[1]);         // a (size)x(size+1)-matrix of int's
  for ( long j=0; j<p.columns(); ++j) {  // loop over columns
    for ( long i=0; i<p.rows(); ++i) { // loop over rows
	typename TImage::IndexType ind;
	ind[0]=i;
	ind[1]=j;
	TComp val=p_img->GetPixel(ind);
	p(i,j) = val;  // to access matrix coefficients,
      }
  }
  return p;
}

template <class TImage,class TComp>
vnl_matrix<TComp> 
DeleteRow(vnl_matrix<TComp> p_in , unsigned int row)
{
  typedef vnl_matrix<TComp> vMatrix;
  unsigned int nrows=p_in.rows()-1;
  if ( row >= nrows ) nrows=p_in.rows();
  vMatrix p(nrows,p_in.columns());      
  unsigned int rowct=0;
  for ( long i=0; i<p.rows(); ++i) { // loop over rows
    if ( i != row ) {
      p.set_row(rowct,p_in.get_row(i));
      rowct++;
    }
  }
  return p;
}

class myRandIntClass
{
public:
myRandIntClass() {}
int operator() (int aRange)
{
  //srand(42);//seed of your choice here
srand ( time(NULL) );
int result = rand() % aRange;
return result;
}
};


template <class TComp>
vnl_matrix<TComp> 
PermuteMatrix( vnl_matrix<TComp> q , bool doperm=true)
{
  typedef vnl_matrix<TComp> vMatrix;
  typedef vnl_vector<TComp> vVector;
  myRandIntClass myRand;

  std::vector<unsigned long> permvec; 
  for (unsigned long i=0; i < q.rows(); i++)
    permvec.push_back(i);
  std::random_shuffle(permvec.begin(), permvec.end(),myRand);
  //  for (unsigned long i=0; i < q.rows(); i++)
  //  std::cout << " permv " << i << " is " << permvec[i] << std::endl;
  std::random_shuffle(permvec.begin(), permvec.end());
  std::random_shuffle(permvec.begin(), permvec.end());
  // for (unsigned long i=0; i < q.rows(); i++)
  //  std::cout << " permv " << i << " is " << permvec[i] << std::endl;
  // 1. permute q
  vMatrix q_perm(q.rows(),q.columns()); 
  for (unsigned long i=0; i < q.rows(); i++)
    { 
      unsigned long perm=permvec[i];
      if ( doperm ) 
	q_perm.set_row(i,q.get_row(perm));
      else q_perm.set_row(i,q.get_row(i));
    }
  return q_perm;
}


template <unsigned int ImageDimension, class PixelType>
int matrixOperation( itk::ants::CommandLineParser::OptionType *option,
  itk::ants::CommandLineParser::OptionType *outputOption = NULL )
{
  std::string funcName=std::string("matrixOperation");
  typedef itk::Image<PixelType, ImageDimension> ImageType;
  typedef double  matPixelType;
  typedef itk::Image<matPixelType,2> MatrixImageType;
  typename ImageType::Pointer outputImage = NULL;

  //   option->SetUsageOption( 2, "multires_matrix_invert[list.txt,maskhighres.nii.gz,masklowres.nii.gz,matrix.mhd]" );

  std::string value = option->GetValue( 0 );
  if( strcmp( value.c_str(), "multires_matrix_invert" ) == 0 )
    {
      std::string listfn=option->GetParameter( 0 );
      std::string maskhfn=option->GetParameter( 1 );
      std::string masklfn=option->GetParameter( 2 );
      //      vnl_matrix<matPixelType> matrixinv=MultiResMatrixInvert<ImageDimension,matPixelType>( listfn, maskhfn, masklfn );
    }
  
  return EXIT_SUCCESS;

}


template <unsigned int ImageDimension, class PixelType>
typename itk::Image<PixelType,2>::Pointer 
ConvertImageListToMatrix( std::string imagelist, std::string maskfn   ) 
{
  typedef itk::Image<PixelType,ImageDimension> ImageType;
  typedef itk::Image<PixelType,2> MatrixImageType;
  typedef itk::ImageFileReader<ImageType> ReaderType;
  typename ReaderType::Pointer reader1 = ReaderType::New();
  reader1->SetFileName( maskfn );
  reader1->Update();
  unsigned long voxct=0;  
  typedef itk::ImageRegionIteratorWithIndex<ImageType> Iterator;
  Iterator mIter( reader1->GetOutput(),reader1->GetOutput()->GetLargestPossibleRegion() );
  for(  mIter.GoToBegin(); !mIter.IsAtEnd(); ++mIter )
    if (mIter.Get() >= 0.5) voxct++;

  std::vector<std::string> image_fn_list;
  // first, count the number of files
  const unsigned int maxChar = 512;
  char lineBuffer[maxChar]; 
  char filenm[maxChar];
  unsigned int filecount=0;
  {
    std::ifstream inputStreamA( imagelist.c_str(), std::ios::in );
    if ( !inputStreamA.is_open() )
      {
	std::cout << "Can't open image list file: " << imagelist << std::endl;  
	return NULL;
      }
	while ( !inputStreamA.eof() )
	  {
	    inputStreamA.getline( lineBuffer, maxChar, '\n' ); 
      	    if ( sscanf( lineBuffer, "%s ",filenm) != 1 ){
	      continue;
	    }
	    else {
	      image_fn_list.push_back(std::string(filenm));
	      filecount++;
	    }
	  }
	inputStreamA.close();  
  }

      /** declare the output matrix image */
      unsigned long xsize=image_fn_list.size();
      unsigned long ysize=voxct;
      typename MatrixImageType::SizeType tilesize;
      tilesize[0]=xsize;
      tilesize[1]=ysize;
      typename MatrixImageType::RegionType region;
      region.SetSize( tilesize );
      typename MatrixImageType::Pointer matimage=MatrixImageType::New();
      matimage->SetLargestPossibleRegion( region );
      matimage->SetBufferedRegion( region );
      typename MatrixImageType::DirectionType mdir;  mdir.Fill(0); mdir[0][0]=1; mdir[1][1]=1;
      typename MatrixImageType::SpacingType mspc;  mspc.Fill(1);
      typename MatrixImageType::PointType morg;  morg.Fill(0);
      matimage->SetSpacing( mspc );
      matimage->SetDirection(mdir);
      matimage->SetOrigin( morg );
      matimage->Allocate();      
      for (unsigned int j=0; j< image_fn_list.size(); j++)
	{
	  typename ReaderType::Pointer reader2 = ReaderType::New();
	  reader2->SetFileName( image_fn_list[j] );
	  reader2->Update();
	  unsigned long xx=0,yy=0,tvoxct=0;
	  xx=j; 
	  typename MatrixImageType::IndexType mind;
	  for(  mIter.GoToBegin(); !mIter.IsAtEnd(); ++mIter )
	    {
	      if (mIter.Get() >= 0.5)
		{
		  yy=tvoxct; 
		  mind[0]=xx;
		  mind[1]=yy;
		  matimage->SetPixel(mind,reader2->GetOutput()->GetPixel(mIter.GetIndex()));
		  tvoxct++;
		}
	    }
	}
  return matimage;
}


template <unsigned int ImageDimension, class PixelType>
int SCCA_vnl( itk::ants::CommandLineParser *parser, unsigned int permct  )
{
  itk::ants::CommandLineParser::OptionType::Pointer outputOption =
    parser->GetOption( "output" );
  if( !outputOption || outputOption->GetNumberOfValues() == 0 )
    {
    std::cerr << "Warning:  no output option set." << std::endl;
    }
  itk::ants::CommandLineParser::OptionType::Pointer option =
    parser->GetOption( "matrix-pair-operation" );
  typedef itk::Image<PixelType, ImageDimension> ImageType;
  typedef double  Scalar;
  typedef itk::ants::antsSCCANObject<ImageType,Scalar>  SCCANType;
  typedef itk::Image<Scalar,2> MatrixImageType;
  typedef itk::ImageFileReader<MatrixImageType> matReaderType;
  typedef itk::ImageFileReader<ImageType> imgReaderType;
  typename SCCANType::Pointer sccanobj=SCCANType::New();
  typedef typename SCCANType::MatrixType         vMatrix;
  typedef typename SCCANType::VectorType         vVector;
  typedef typename SCCANType::DiagonalMatrixType dMatrix;
  Scalar pinvtoler=1.e-6;
  /** read the matrix images */
  typename matReaderType::Pointer matreader1 = matReaderType::New();
  matreader1->SetFileName( option->GetParameter( 0 ) );
  matreader1->Update();
 
  typename matReaderType::Pointer matreader2 = matReaderType::New();
  matreader2->SetFileName( option->GetParameter( 1 ) );
  matreader2->Update();
 
  typename imgReaderType::Pointer imgreader1 = imgReaderType::New();
  imgreader1->SetFileName( option->GetParameter( 2 ) );
  imgreader1->Update();
  typename ImageType::Pointer mask1=imgreader1->GetOutput();
 
  typename imgReaderType::Pointer imgreader2 = imgReaderType::New();
  imgreader2->SetFileName( option->GetParameter( 3 ) );
  imgreader2->Update();
  typename ImageType::Pointer mask2=imgreader2->GetOutput();

  /** the penalties define the fraction of non-zero values for each view */
  double FracNonZero1 = parser->Convert<double>( option->GetParameter( 4 ) );
  if ( FracNonZero1 < 0 )
    {
      FracNonZero1=fabs(FracNonZero1);
      sccanobj->SetKeepPositiveP(false);
    }
  double FracNonZero2 = parser->Convert<double>( option->GetParameter( 5 ) );
  if ( FracNonZero2 < 0 )
    {
      FracNonZero2=fabs(FracNonZero2);
      sccanobj->SetKeepPositiveQ(false);
    }

  /** we refer to the two view matrices as P and Q */
  vMatrix p=CopyImageToVnlMatrix<MatrixImageType,Scalar>( matreader1->GetOutput() );
  vMatrix q=CopyImageToVnlMatrix<MatrixImageType,Scalar>( matreader2->GetOutput() );

  sccanobj->SetFractionNonZeroP(FracNonZero1);
  sccanobj->SetFractionNonZeroQ(FracNonZero2);
  sccanobj->SetMatrixP( p );
  sccanobj->SetMatrixQ( q );
  sccanobj->SetMaskImageP( mask1 );
  sccanobj->SetMaskImageQ( mask2 );

  double truecorr=sccanobj->RunSCCAN2();
  vVector w_p=sccanobj->GetPWeights();
  vVector w_q=sccanobj->GetQWeights();
  std::cout <<"  length p " << p.rows() << " wp " << w_p.size() << std::endl;
  std::cout << " true-corr " << truecorr << std::endl; 
  std::cout << " Projection-P " << p*w_p << std::endl;
  std::cout << " Projection-Q " << q*w_q << std::endl;
 //  double corr=vnl_pearson_corr<Scalar>( p*w_p , q*w_q );
  //  std::cout << " w_q " << w_q<< std::endl;
 
  if( outputOption )
    {
      std::string filename =  outputOption->GetValue( 0 );
      std::cout << " write " << filename << std::endl;
      std::string post=std::string("View1vec");
      WriteVectorToSpatialImage<ImageType,Scalar>( filename, post, w_p , mask1);
      post=std::string("View2vec");
      WriteVectorToSpatialImage<ImageType,Scalar>( filename, post, w_q , mask2);
    }

  /** begin permutation 1. q_pvMatrix CqqInv=vnl_svd_inverse<Scalar>(Cqq);
   q=q*CqqInv;
  sermuted ;  2. scca ;  3. test corrs and weights significance */
  if ( permct > 0 ) {
  unsigned long perm_exceed_ct=0;
  vVector w_p_signif_ct(w_p.size(),0);
  vVector w_q_signif_ct(w_q.size(),0);
  for (unsigned long pct=0; pct<=permct; pct++)
    {
      // 0. compute permutation for q ( switch around rows ) 
      vMatrix q_perm=PermuteMatrix<Scalar>( sccanobj->GetMatrixQ() );
      sccanobj->SetMatrixQ( q_perm );
      double permcorr=sccanobj->RunSCCAN2();
      if ( permcorr > truecorr ) perm_exceed_ct++;
      vVector w_p_perm=sccanobj->GetPWeights();
      vVector w_q_perm=sccanobj->GetQWeights();
      for (unsigned long j=0; j<w_p.size(); j++)
	if ( w_p_perm(j) > w_p(j)) 
	  {
	    w_p_signif_ct(j)=w_p_signif_ct(j)++;
	  }
      for (unsigned long j=0; j<w_q.size(); j++)
	if ( w_q_perm(j) > w_q(j) ) 
	  {
	    w_q_signif_ct(j)=w_q_signif_ct(j)++;
	  }	
      // end solve cca permutation
      std::cout << permcorr << " overall " <<  (double)perm_exceed_ct/(pct+1) << " ct " << pct << " true " << truecorr << std::endl; 
    }
  unsigned long psigct=0,qsigct=0;
  for (unsigned long j=0; j<w_p.size(); j++){
    if ( w_p(j) > pinvtoler ) {
      w_p_signif_ct(j)=1.0-(double)w_p_signif_ct(j)/(double)(permct);
      if ( w_p_signif_ct(j) > 0.949 ) psigct++;
    } else w_p_signif_ct(j)=0;
  }
  for (unsigned long j=0; j<w_q.size(); j++) {
    if ( w_q(j) > pinvtoler ) {
      w_q_signif_ct(j)=1.0-(double)w_q_signif_ct(j)/(double)(permct);
      if ( w_q_signif_ct(j) > 0.949 ) qsigct++;
    } else w_q_signif_ct(j)=0;
    }
  std::cout <<  " overall " <<  (double)perm_exceed_ct/(permct) << " ct " << permct << std::endl;
  std::cout << " p-vox " <<  (double)psigct/w_p.size() << " ct " << permct << std::endl;
  std::cout << " q-vox " <<  (double)qsigct/w_q.size() << " ct " << permct << std::endl;

    if( outputOption )
    { 
      std::string filename =  outputOption->GetValue( 0 );
      std::cout << " write " << filename << std::endl;
      std::string post=std::string("View1pval");
      WriteVectorToSpatialImage<ImageType,Scalar>( filename, post, w_p_signif_ct , mask1);
      post=std::string("View2pval");
      WriteVectorToSpatialImage<ImageType,Scalar>( filename, post, w_q_signif_ct , mask2);
    }
  }
  return EXIT_SUCCESS;
}

template <unsigned int ImageDimension, class PixelType>
int mSCCA_vnl( itk::ants::CommandLineParser *parser,
	       unsigned int permct , bool run_partial_scca = false )
{
  std::cout <<" Entering MSCCA " << std::endl;
  itk::ants::CommandLineParser::OptionType::Pointer outputOption =
    parser->GetOption( "output" );
  if( !outputOption || outputOption->GetNumberOfValues() == 0 )
    {
    std::cerr << "Warning:  no output option set." << std::endl;
    }
  itk::ants::CommandLineParser::OptionType::Pointer option =
    parser->GetOption( "matrix-pair-operation" );
  std::cout << outputOption << std::endl;
  typedef itk::Image<PixelType, ImageDimension> ImageType;
  typedef double  Scalar;
  typedef itk::ants::antsSCCANObject<ImageType,Scalar>  SCCANType;
  typedef itk::Image<Scalar,2> MatrixImageType;
  typedef itk::ImageFileReader<MatrixImageType> matReaderType;
  typedef itk::ImageFileReader<ImageType> imgReaderType;
  typename SCCANType::Pointer sccanobj=SCCANType::New();
  typedef typename SCCANType::MatrixType         vMatrix;
  typedef typename SCCANType::VectorType         vVector;
  typedef typename SCCANType::DiagonalMatrixType dMatrix;

  /** we refer to the two view matrices as P and Q */
  typedef itk::Image<PixelType, ImageDimension> ImageType;
  typedef double  Scalar;
  typedef itk::Image<Scalar,2> MatrixImageType;
  typedef itk::ImageFileReader<MatrixImageType> matReaderType;
  typedef itk::ImageFileReader<ImageType> imgReaderType;

  /** read the matrix images */
  typename matReaderType::Pointer matreader1 = matReaderType::New();
  std::cout << option->GetParameter( 0 ) << std::endl;
  matreader1->SetFileName( option->GetParameter( 0 ) );
  matreader1->Update();
 
  typename matReaderType::Pointer matreader2 = matReaderType::New();
  matreader2->SetFileName( option->GetParameter( 1 ) );
  matreader2->Update();
  
  typename matReaderType::Pointer matreader3 = matReaderType::New();
  matreader3->SetFileName( option->GetParameter( 2 ) );
  matreader3->Update();
 
  typename imgReaderType::Pointer imgreader1 = imgReaderType::New();
  imgreader1->SetFileName( option->GetParameter( 3 ) );
  imgreader1->Update();
  typename ImageType::Pointer mask1=imgreader1->GetOutput();
 
  typename imgReaderType::Pointer imgreader2 = imgReaderType::New();
  imgreader2->SetFileName( option->GetParameter( 4 ) );
  imgreader2->Update();
  typename ImageType::Pointer mask2=imgreader2->GetOutput();

  typename imgReaderType::Pointer imgreader3 = imgReaderType::New();
  imgreader3->SetFileName( option->GetParameter( 5 ) );
  imgreader3->Update();
  typename ImageType::Pointer mask3=imgreader3->GetOutput();

  /** the penalties define the fraction of non-zero values for each view */
  double FracNonZero1 = parser->Convert<double>( option->GetParameter( 6 ) );
  if ( FracNonZero1 < 0 )
    {
      FracNonZero1=fabs(FracNonZero1);
      sccanobj->SetKeepPositiveP(false);
    }
  double FracNonZero2 = parser->Convert<double>( option->GetParameter( 7 ) );
  if ( FracNonZero2 < 0 )
    {
      FracNonZero2=fabs(FracNonZero2);
      sccanobj->SetKeepPositiveQ(false);
    }
  double FracNonZero3 = parser->Convert<double>( option->GetParameter( 8 ) );
  if ( FracNonZero3 < 0 )
    {
      FracNonZero3=fabs(FracNonZero3);
      sccanobj->SetKeepPositiveR(false);
    }
  vMatrix pin=CopyImageToVnlMatrix<MatrixImageType,Scalar>( matreader1->GetOutput() );
  vMatrix qin=CopyImageToVnlMatrix<MatrixImageType,Scalar>( matreader2->GetOutput() );
  vMatrix rin=CopyImageToVnlMatrix<MatrixImageType,Scalar>( matreader3->GetOutput() );

  sccanobj->SetFractionNonZeroP(FracNonZero1);
  sccanobj->SetFractionNonZeroQ(FracNonZero2);
  sccanobj->SetFractionNonZeroR(FracNonZero3);
  
  for ( unsigned int leave_out=pin.rows(); leave_out <= pin.rows();  leave_out++) {
    std::cout << " Leaving Out " << leave_out << std::endl;
    vVector p_leave_out;
    vVector q_leave_out;
  if ( leave_out < pin.rows() ) {
    p_leave_out=pin.get_row(leave_out);
    q_leave_out=qin.get_row(leave_out);
  }
  vMatrix p=DeleteRow<MatrixImageType,Scalar>( pin , leave_out );
  vMatrix q=DeleteRow<MatrixImageType,Scalar>( qin , leave_out );
  vMatrix r=DeleteRow<MatrixImageType,Scalar>( rin , leave_out );
  double truecorr=0;
  if ( run_partial_scca ){ 
    std::cout <<" begin partial PQ " << std::endl;
    typename SCCANType::Pointer sccanobjCovar=SCCANType::New();
    sccanobjCovar->SetMatrixP( p );
    sccanobjCovar->SetMatrixQ( q );
    sccanobjCovar->SetMatrixR( r );
    sccanobjCovar->SetFractionNonZeroP(FracNonZero1);
    sccanobjCovar->SetFractionNonZeroQ(FracNonZero2);
    sccanobjCovar->SetMaskImageP( mask1 );
    sccanobjCovar->SetMaskImageQ( mask2 );
    truecorr=sccanobjCovar->RunSCCAN2();
    std::cout << " partialed out corr " << truecorr << std::endl;

  vVector w_p=sccanobjCovar->GetPWeights();
  vVector w_q=sccanobjCovar->GetQWeights();
  std::cout <<"  length p " << p.rows() << " wp " << w_p.size() << std::endl;
  std::cout << " true-corr " << truecorr << std::endl; 
  std::cout << " Projection-P " << p*w_p << std::endl;
  std::cout << " Projection-Q " << q*w_q << std::endl;
 //  double corr=vnl_pearson_corr<Scalar>( p*w_p , q*w_q );
  //  std::cout << " w_q " << w_q<< std::endl;
 
  if( outputOption )
    {
      std::string filename =  outputOption->GetValue( 0 );
      std::cout << " write " << filename << std::endl;
      std::string post=std::string("View1vec");
      WriteVectorToSpatialImage<ImageType,Scalar>( filename, post, w_p , mask1);
      post=std::string("View2vec");
      WriteVectorToSpatialImage<ImageType,Scalar>( filename, post, w_q , mask2);
    }

  /** begin permutation 1. q_pvMatrix CqqInv=vnl_svd_inverse<Scalar>(Cqq);
   q=q*CqqInv;
  sermuted ;  2. scca ;  3. test corrs and weights significance */
  if ( permct > 0 ) {
  unsigned long perm_exceed_ct=0;
  vVector w_p_signif_ct(w_p.size(),0);
  vVector w_q_signif_ct(w_q.size(),0);
  for (unsigned long pct=0; pct<=permct; pct++)
    {
      // 0. compute permutation for q ( switch around rows ) 
      vMatrix p_perm=PermuteMatrix<Scalar>( sccanobjCovar->GetMatrixP() );
      sccanobjCovar->SetMatrixP( p_perm );
      sccanobjCovar->SetMatrixQ( sccanobjCovar->GetMatrixQ() );
      sccanobjCovar->SetMatrixR( sccanobjCovar->GetMatrixR() );
      double permcorr=sccanobjCovar->RunSCCAN2();
      if ( permcorr > truecorr ) perm_exceed_ct++;
      vVector w_p_perm=sccanobjCovar->GetPWeights();
      vVector w_q_perm=sccanobjCovar->GetQWeights();
      for (unsigned long j=0; j<w_p.size(); j++)
	if ( w_p_perm(j) > w_p(j)) 
	  {
	    w_p_signif_ct(j)=w_p_signif_ct(j)++;
	  }
      for (unsigned long j=0; j<w_q.size(); j++)
	if ( w_q_perm(j) > w_q(j) ) 
	  {
	    w_q_signif_ct(j)=w_q_signif_ct(j)++;
	  }	
      // end solve cca permutation
      std::cout << permcorr << " overall " <<  (double)perm_exceed_ct/(pct+1) << " ct " << pct << " true " << truecorr << std::endl; 
    }
  unsigned long psigct=0,qsigct=0;
  Scalar pinvtoler=1.e-6;
  for (unsigned long j=0; j<w_p.size(); j++){
    if ( w_p(j) > pinvtoler ) {
      w_p_signif_ct(j)=1.0-(double)w_p_signif_ct(j)/(double)(permct);
      if ( w_p_signif_ct(j) > 0.949 ) psigct++;
    } else w_p_signif_ct(j)=0;
  }
  for (unsigned long j=0; j<w_q.size(); j++) {
    if ( w_q(j) > pinvtoler ) {
      w_q_signif_ct(j)=1.0-(double)w_q_signif_ct(j)/(double)(permct);
      if ( w_q_signif_ct(j) > 0.949 ) qsigct++;
    } else w_q_signif_ct(j)=0;
    }
  std::cout <<  " overall " <<  (double)perm_exceed_ct/(permct) << " ct " << permct << std::endl;
  std::cout << " p-vox " <<  (double)psigct/w_p.size() << " ct " << permct << std::endl;
  std::cout << " q-vox " <<  (double)qsigct/w_q.size() << " ct " << permct << std::endl;

    if( outputOption )
    { 
      std::string filename =  outputOption->GetValue( 0 );
      std::cout << " write " << filename << std::endl;
      std::string post=std::string("View1pval");
      WriteVectorToSpatialImage<ImageType,Scalar>( filename, post, w_p_signif_ct , mask1);
      post=std::string("View2pval");
      WriteVectorToSpatialImage<ImageType,Scalar>( filename, post, w_q_signif_ct , mask2);
    }
  }

    exit(0);
  }
  std::cout << " VNL mSCCA " << std::endl;
  sccanobj->SetMatrixP( p );
  sccanobj->SetMatrixQ( q );
  sccanobj->SetMatrixR( r );
  sccanobj->SetMaskImageP( mask1 );
  sccanobj->SetMaskImageQ( mask2 );
  sccanobj->SetMaskImageR( mask3 );
  truecorr=sccanobj->RunSCCAN3();
  vVector w_p=sccanobj->GetPWeights();
  vVector w_q=sccanobj->GetQWeights();
  vVector w_r=sccanobj->GetRWeights();
  std::cout << " final correlation  " << truecorr  << std::endl;
  std::cout << " Projection-P " << p*w_p << std::endl;
  std::cout << " Projection-Q " << q*w_q << std::endl;
  if ( leave_out < pin.rows() ) {
    std::cout << " Projection-leave-P " << dot_product(p_leave_out,w_p) << std::endl;
    std::cout << " Projection-leave-Q " << dot_product(q_leave_out,w_q) << std::endl;
  }
  //  std::cout <<  " r weights " << w_r << std::endl;
  for (unsigned long j=0; j<w_r.size(); j++) {
    if ( w_r(j) > 0) 
      std::cout << " r-weight " << j << "," << w_r(j) << std::endl;
  }
  if( outputOption )
    {
      std::string filename =  outputOption->GetValue( 0 );
      std::cout << " write " << filename << std::endl;
      std::string post=std::string("View1vec");
      WriteVectorToSpatialImage<ImageType,Scalar>( filename, post, w_p , mask1);
      post=std::string("View2vec");
      WriteVectorToSpatialImage<ImageType,Scalar>( filename, post, w_q , mask2);
      post=std::string("View3vec");
      WriteVectorToSpatialImage<ImageType,Scalar>( filename, post, w_r , mask3);
    }

  /** begin permutation 1. q_pvMatrix CqqInv=vnl_svd_inverse<Scalar>(Cqq);
   q=q*CqqInv;
  sermuted ;  2. scca ;  3. test corrs and weights significance */
  unsigned long perm_exceed_ct=0;
  if ( permct > 0 ) {
  vVector w_p_signif_ct(w_p.size(),0);
  vVector w_q_signif_ct(w_q.size(),0);
  vVector w_r_signif_ct(w_r.size(),0);
  for (unsigned long pct=0; pct<=permct; pct++)
    {
      // 0. compute permutation for q ( switch around rows ) 
      //std::cout << " dont permute q " << std::endl;
      vMatrix q_perm=PermuteMatrix<Scalar>( sccanobj->GetMatrixQ() );
      vMatrix r_perm=PermuteMatrix<Scalar>( sccanobj->GetMatrixR() );
      sccanobj->SetMatrixQ( q_perm );
      sccanobj->SetMatrixR( r_perm );
      double permcorr=sccanobj->RunSCCAN3();
      if ( permcorr > truecorr ) perm_exceed_ct++;
      vVector w_p_perm=sccanobj->GetPWeights();
      vVector w_q_perm=sccanobj->GetQWeights();
      vVector w_r_perm=sccanobj->GetRWeights();

      for (unsigned long j=0; j<w_r.size(); j++)
	if ( w_r_perm(j) > w_r(j)) 
	  {
	    w_r_signif_ct(j)=w_r_signif_ct(j)++;
	  }
      //      std::cout << " only testing correlation with biserial predictions " << std::endl;
      // end solve cca permutation
      std::cout << permcorr << " overall " <<  (double)perm_exceed_ct/(pct+1) << " ct " << pct << " true " << truecorr << std::endl; 
      for (unsigned long j=0; j<w_r.size(); j++) {
	if ( w_r(j) > 0) 
	std::cout << " r entry " << j << " signif " <<  (double)w_r_signif_ct(j)/(double)(pct+1) << std::endl;
      }

    }
  }
  //  std::cout <<  " overall " <<  (double)perm_exceed_ct/(permct+1) << " ct " << permct << std::endl;
  }
  return EXIT_SUCCESS;
}


int sumba( itk::ants::CommandLineParser *parser )
{
  // Get dimensionality
  
  itk::ants::CommandLineParser::OptionType::Pointer outputOption =
    parser->GetOption( "output" );
  if( !outputOption || outputOption->GetNumberOfValues() == 0 )
    {
    std::cerr << "Warning:  no output option set." << std::endl;
    }

  unsigned int permct=0;
  itk::ants::CommandLineParser::OptionType::Pointer permoption =
    parser->GetOption( "n_permutations" );
  if( !permoption || permoption->GetNumberOfValues() == 0 )
    {
    std::cerr << "Warning:  no output option set." << std::endl;
    }
  else permct=parser->Convert<unsigned int>( permoption->GetValue() );
  std::cout <<" you will assess significance with " << permct << " permutations." << std::endl;
  //  operations on individual matrices
  itk::ants::CommandLineParser::OptionType::Pointer matrixOption =
    parser->GetOption( "matrix-operation" );
  itk::ants::CommandLineParser::OptionType::Pointer matrixPairOption =
    parser->GetOption( "matrix-pair-operation" );

  if( matrixOption && matrixOption->GetNumberOfValues() > 0 )
    {
      matrixOperation<2, double>( matrixOption, outputOption );
      return EXIT_SUCCESS;
    }
  //  operations on pairs of matrices
  else if( matrixPairOption && matrixPairOption->GetNumberOfValues() > 0 )
    {
      if( matrixPairOption && matrixPairOption->GetNumberOfParameters() < 2 )
    { 
      std::cerr << "  Incorrect number of parameters."<<  std::endl;
    return EXIT_FAILURE;
    }
  typedef double PixelType;
  typedef itk::Image<PixelType, 3> ImageType;
  typedef double  matPixelType;
  typedef itk::Image<matPixelType,2> MatrixImageType;
  typedef itk::ImageFileReader<MatrixImageType> ReaderType;
  std::string initializationStrategy = matrixPairOption->GetValue();
  // call RCCA_eigen or RCCA_vnl 
  if (  !initializationStrategy.compare( std::string( "scca_vnl" ) )  ) 
  {
    std::cout << " scca_vnl "<< std::endl;
    SCCA_vnl<3, double>( parser , permct );
  }
  else if (  !initializationStrategy.compare( std::string("mscca_vnl") )  ) 
  {
    std::cout << " mscca_vnl "<< std::endl;
    mSCCA_vnl<3, double>( parser, permct,  false );
  }
  else if ( !initializationStrategy.compare( std::string("pscca_vnl") )   ) 
  {
    std::cout << " pscca_vnl "<< std::endl;
    mSCCA_vnl<3, double>( parser, permct , true );
  }
  else 
  {
    std::cout <<" unrecognized option in matrixPairOperation " << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;

      return EXIT_SUCCESS;
    }
  else {
    std::cout << " no option specified " << std::endl;
  }
 

  return EXIT_SUCCESS;
}

void InitializeCommandLineOptions( itk::ants::CommandLineParser *parser )
{
  /** in this function, list all the operations you will perform */ 

  typedef itk::ants::CommandLineParser::OptionType OptionType;
  {
  std::string description = std::string( "Print the help menu (short version)." );

  OptionType::Pointer option = OptionType::New();
  option->SetShortName( 'h' );
  option->SetDescription( description );
  option->AddValue( std::string( "0" ) );
  parser->AddOption( option );
  }

  {
  std::string description = std::string( "Print the help menu (long version)." );

  OptionType::Pointer option = OptionType::New();
  option->SetLongName( "help" );
  option->SetDescription( description );
  option->AddValue( std::string( "0" ) );
  parser->AddOption( option );
  }

  {
  std::string description =
    std::string( "Ouput dependent on which option is called." );

  OptionType::Pointer option = OptionType::New();
  option->SetLongName( "output" );
  option->SetShortName( 'o' );
  option->SetUsageOption( 0, "outputImage" );
  option->SetDescription( description );
  parser->AddOption( option );
  }

  {
  std::string description =
    std::string( "Number of permutations to use in scca." );
  OptionType::Pointer option = OptionType::New();
  option->SetLongName( "n_permutations" );
  option->SetShortName( 'p' );
  option->SetUsageOption( 0, "500" );
  option->SetDescription( description );
  parser->AddOption( option );
  }



  {
  std::string description =
    std::string( "Matrix operations such as invert," ) +
    std::string( "multiply, etc." );

  OptionType::Pointer option = OptionType::New();
  option->SetLongName( "matrix-operation" );
  option->SetUsageOption( 0, "invert[matrix.mhd]" );
  option->SetUsageOption( 1, "ridge[matrix.mhd]" );
  option->SetUsageOption( 2, "multires_matrix_invert[list.txt,maskhighres.nii.gz,masklowres.nii.gz,matrix.mhd]" );
  option->SetDescription( description );
  parser->AddOption( option );
  }

  {
  std::string description =
    std::string( "Matrix-based scca operations for 2 and 3 views." ) +
    std::string( "For {m,p}scca_vnl, the FracNonZero terms set the fraction of variables to use in the estimate. E.g. if one sets 0.5 then half of the variables will have non-zero values.  If the FracNonZero is (+) then the weight vectors must be positive.  If they are negative, weights can be (+) or (-). pscca_vnl does partial scca for 2 views while partialing out the 3rd view. ");
  OptionType::Pointer option = OptionType::New();
  option->SetLongName( "matrix-pair-operation" );
  option->SetUsageOption( 0, "scca_vnl[matrix-view1.mhd,matrix-view2.mhd,mask1,mask2,FracNonZero1,FracNonZero2] ");
  option->SetUsageOption( 1, "mscca_vnl[matrix-view1.mhd,matrix-view2.mhd,matrix-view3.mhd,FracNonZero1,FracNonZero2,FracNonZero3]" );
  option->SetUsageOption( 2, "pscca_vnl[matrix-view1.mhd,matrix-view2.mhd,matrix-view3.mhd,FracNonZero1,FracNonZero2,FracNonZero3]" );
  option->SetDescription( description );
  parser->AddOption( option );
  }


}


int main( int argc, char *argv[] )
{

  itk::ants::CommandLineParser::Pointer parser =
    itk::ants::CommandLineParser::New();
  parser->SetCommand( argv[0] );

  std::string commandDescription =
    std::string( "A tool for sparse statistical analysis and matrix operations on images : " ) +
    std::string( " works on individual images or image sets.  " );

  parser->SetCommandDescription( commandDescription );
  InitializeCommandLineOptions( parser );

  parser->Parse( argc, argv );

  // Print the entire help menu
  itk::ants::CommandLineParser::OptionType::Pointer longHelpOption =
    parser->GetOption( "help" );
  if( argc == 1 ||
    ( longHelpOption && parser->Convert<unsigned int>( longHelpOption->GetValue() ) == 1 ) 
      )
    {
    parser->PrintMenu( std::cout, 5, false );
    exit( EXIT_FAILURE );
    }

  itk::ants::CommandLineParser::OptionType::Pointer shortHelpOption =
    parser->GetOption( 'h' );
  if( argc == 1 || ( shortHelpOption &&
    parser->Convert<unsigned int>( shortHelpOption->GetValue() ) == 1 ) )
    {
    parser->PrintMenu( std::cout, 5, true );
    exit( EXIT_FAILURE );
    }

  // Print the long help menu for specific items
  if( longHelpOption && longHelpOption->GetNumberOfValues() > 0
    && parser->Convert<unsigned int>( longHelpOption->GetValue() ) != 0 )
    {
    itk::ants::CommandLineParser::OptionListType options =
      parser->GetOptions();
    for( unsigned int n = 0; n < longHelpOption->GetNumberOfValues(); n++ )
      {
      std::string value = longHelpOption->GetValue( n );
      itk::ants::CommandLineParser::OptionListType::const_iterator it;
      for( it = options.begin(); it != options.end(); ++it )
        {
        const char *longName = ( ( *it )->GetLongName() ).c_str();
        if( strstr( longName, value.c_str() ) == longName  )
          {
          parser->PrintMenu( std::cout, 5, false );
          }
        }
      }
    exit( EXIT_FAILURE );
    }


  // Call main routine
  sumba( parser );

  exit( EXIT_SUCCESS );

}


  // now compute covariance matrices 
  // covariance matrix --- Cov(X, Y) = E[XY] - E[X].E[Y]
  /* input matrix
ta<-matrix(c(-1,1,2,2,-2,3,1,1,4,0,3,4),nrow=3,ncol=4)
 ta<-matrix(c(-1,1,2,-2,3,1,4,0,3),nrow=3,ncol=3)

> ta
     [,1] [,2] [,3]
[1,]   -1    1    2
[2,]   -2    3    1
[3,]    4    0    3

  // cov(ta,ta)
          [,1]      [,2] [,3]
[1,] 10.333333 -4.166667  3.0
[2,] -4.166667  2.333333 -1.5
[3,]  3.000000 -1.500000  1.0

> cov(a[1,]-mean(a[1,]),a[1,]-mean(a[1,])) 
[1] 10.33333
v<-a[1,]-mean(a[1,])
> v*v
[1]  1.777778  5.444444 13.444444
> sum(v*v)
[1] 20.66667
> sum(v*v)/(2)  <-  sum( v*v ) / ( n - 1 ) = covariance of two vectors ... 
[1] 10.33333
  */

   /** try  q.colwise.sum() to compute mean ... */
   /** try  q.rowwise.sum() to compute mean ... 
     
 eMatrix testM(3,3);
testM(0,0)=-1;testM(1,0)=1; testM(2,0)=2;
testM(0,1)=-2;testM(1,1)=3; testM(2,1)=1;
testM(0,2)= 4;testM(1,2)=0; testM(2,2)=3;
 p=testM;
 pMatSize[0]=3; 
 pMatSize[1]=3; 
   */


  /*
						  //1.0/(double)q.columns(); //randgen.drand32();
  for (unsigned int it=0; it<4; it++)
  {
    //    std::cout << " 2norm(v0) " << v_0.two_norm() << std::endl;
    vVector v_1=(q)*v_0;
    double vnorm=v_1.two_norm();
    std::cout << " von " << vnorm << std::endl;
    v_0=v_1/(vnorm);
    std::cout << " vo " << v_0 << std::endl;
  // check if power method works .... 
  vVector Xv=q*v_0;
  Scalar vdotXv = dot_product(v_0,Xv); 
  std::cout << " vdotXv " << vdotXv << std::endl;
  vVector Xv2=Xv-v_0*vdotXv;
  // this value should be small -- i.e. v_0 is an eigenvector of X
  std::cout << " init eigenvector result " << Xv2.squared_magnitude() << std::endl;}
*/
  //  std::cout << v_0 << std::endl;
   /*

function [Up,Sp,Vp] = rank_one_svd_update( U, S, V, a, b, force_orth )
% function [Up,Sp,Vp] = rank_one_svd_update( U, S, V, a, b, force_orth )
%
% Given the SVD of
%
%   X = U*S*V'
%
% update it to be the SVD of
%
%   X + ab' = Up*Sp*Vp'
%
% that is, implement a rank-one update to the SVD of X.
%
% Depending on a,b there may be considerable structure that could
% be exploited, but which this code does not.
%
% The subspace rotations involved may not preserve orthogonality due
% to numerical round-off errors.  To compensate, you can set the
% "force_orth" flag, which will force orthogonality via a QR plus
% another SVD.  In a long loop, you may want to force orthogonality
% every so often.
%
% See Matthew Brand, "Fast low-rank modifications of the thin
% singular value decomposition".
%
% D. Wingate 8/17/2007
%

  current_rank = size( U, 2 );

  % P is an orthogonal basis of the column-space
  % of (I-UU')a, which is the component of "a" that is
  % orthogonal to U.
  m = U' * a;
  p = a - U*m;
  Ra = sqrt(p'*p);
  P = (1/Ra)*p;

  % XXX this has problems if a is already in the column space of U!
  % I don't know what to do in that case.
  if ( Ra < 1e-13 )
    fprintf('------> Whoa! No orthogonal component of m!\n');
  end;
  
  % Q is an orthogonal basis of the column-space
  % of (I-VV')b.
  n = V' * b;
  q = b - V*n;
  Rb = sqrt(q'*q);
  Q = (1/Rb)*q;

  if ( Rb < 1e-13 )
    fprintf('------> Whoa! No orthogonal component of n!\n');
  end;
  
  %
  % Diagonalize K, maintaining rank
  %

  % XXX note that this diagonal-plus-rank-one, so we should be able
  % to take advantage of the structure!
  z = zeros( size(m) );

  K = [ S z ; z' 0 ] + [ m; Ra ]*[ n; Rb ]';

  [tUp,tSp,tVp] = svds( K, current_rank );

  %
  % Now update our matrices!
  %
  
  Sp = tSp;

  Up = [ U P ] * tUp;
  Vp = [ V Q ] * tVp;

  % The above rotations may not preserve orthogonality, so we explicitly
  % deal with that via a QR plus another SVD.  In a long loop, you may
  % want to force orthogonality every so often.

  if ( force_orth )
    [UQ,UR] = qr( Up, 0 );
    [VQ,VR] = qr( Vp, 0 );
    [tUp,tSp,tVp] = svds( UR * Sp * VR', current_rank );
    Up = UQ * tUp;
    Vp = VQ * tVp;
    Sp = tSp;
  end;
  
return;
*/
