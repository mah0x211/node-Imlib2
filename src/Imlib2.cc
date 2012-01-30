#include <node.h>
#include <node_events.h>

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/time.h>
#include <limits.h>

#include <cstring>
#include <typeinfo>
#include <pthread.h>
#include "Imlib2.h"

using namespace v8;
using namespace node;

#define ObjectUnwrap(tmpl,obj)  ObjectWrap::Unwrap<tmpl>(obj)
#define IsDefined(v) ( !v->IsNull() && !v->IsUndefined() )

typedef enum {
	NOERR = IMLIB_LOAD_ERROR_NONE,
	FILE_DOES_NOT_EXIST = IMLIB_LOAD_ERROR_FILE_DOES_NOT_EXIST,
	FILE_IS_DIRECTORY = IMLIB_LOAD_ERROR_FILE_IS_DIRECTORY,
	PERMISSION_DENIED_TO_READ = IMLIB_LOAD_ERROR_PERMISSION_DENIED_TO_READ,
	NO_LOADER_FOR_FILE_FORMAT = IMLIB_LOAD_ERROR_NO_LOADER_FOR_FILE_FORMAT,
	PATH_TOO_LONG = IMLIB_LOAD_ERROR_PATH_TOO_LONG,
	PATH_COMPONENT_NON_EXISTANT = IMLIB_LOAD_ERROR_PATH_COMPONENT_NON_EXISTANT,
	PATH_COMPONENT_NOT_DIRECTORY = IMLIB_LOAD_ERROR_PATH_COMPONENT_NOT_DIRECTORY,
	PATH_POINTS_OUTSIDE_ADDRESS_SPACE = IMLIB_LOAD_ERROR_PATH_POINTS_OUTSIDE_ADDRESS_SPACE,
	TOO_MANY_SYMBOLIC_LINKS = IMLIB_LOAD_ERROR_TOO_MANY_SYMBOLIC_LINKS,
	OUT_OF_MEMORY = IMLIB_LOAD_ERROR_OUT_OF_MEMORY,
	OUT_OF_FILE_DESCRIPTORS = IMLIB_LOAD_ERROR_OUT_OF_FILE_DESCRIPTORS,
	PERMISSION_DENIED_TO_WRITE = IMLIB_LOAD_ERROR_PERMISSION_DENIED_TO_WRITE,
	OUT_OF_DISK_SPACE = IMLIB_LOAD_ERROR_OUT_OF_DISK_SPACE,
	UNKNOWN = IMLIB_LOAD_ERROR_UNKNOWN,
	FORMAT_UNACCEPTABLE = 1000
} ImageErrorType_e;

typedef enum {
	ALIGN_NONE,
	ALIGN_LEFT = 1,
	ALIGN_CENTER,
	ALIGN_RIGHT,
	
	ALIGN_TOP = 1,
	ALIGN_MIDDLE,
	ALIGN_BOTTOM
} ImageAlign_e;

typedef struct {
	int w;
	int h;
	double aspect;
} ImageSize;

/*
typedef struct {
    void *ctx;
    // callback js function when async is true
    Persistent<Function> callback;
    void *data;
    eio_req *req;
} Baton_t;
*/

static inline int CurrentTimestamp( char **str )
{
    struct timeval tv;
    
    // ???: needs lock/mutex?
    if( 0 != gettimeofday( &tv, NULL ) || -1 == asprintf( str, "%lld%06d", tv.tv_sec, tv.tv_usec ) ){
        *str = NULL;
        return errno;
    }
    
    return 0;
}

static const char *ImlibStrError( ImageErrorType_e err )
{
	const char *errstr = NULL;
	
	switch( err )
	{
		case NOERR:
			errstr = "NONE";
		break;

		case FILE_DOES_NOT_EXIST:
			errstr = "FILE_DOES_NOT_EXIST";
		break;

		case FILE_IS_DIRECTORY:
			errstr = "FILE_IS_DIRECTORY";
		break;

		case PERMISSION_DENIED_TO_READ:
			errstr = "PERMISSION_DENIED_TO_READ";
		break;

		case NO_LOADER_FOR_FILE_FORMAT:
			errstr = "NO_LOADER_FOR_FILE_FORMAT";
		break;

		case PATH_TOO_LONG:
			errstr = "PATH_TOO_LONG";
		break;

		case PATH_COMPONENT_NON_EXISTANT:
			errstr = "PATH_COMPONENT_NON_EXISTANT";
		break;

		case PATH_COMPONENT_NOT_DIRECTORY:
			errstr = "PATH_COMPONENT_NOT_DIRECTORY";
		break;

		case PATH_POINTS_OUTSIDE_ADDRESS_SPACE:
			errstr = "PATH_POINTS_OUTSIDE_ADDRESS_SPACE";
		break;

		case TOO_MANY_SYMBOLIC_LINKS:
			errstr = "TOO_MANY_SYMBOLIC_LINKS";
		break;

		case OUT_OF_MEMORY:
			errstr = "OUT_OF_MEMORY";
		break;

		case OUT_OF_FILE_DESCRIPTORS:
			errstr = "OUT_OF_FILE_DESCRIPTORS";
		break;

		case PERMISSION_DENIED_TO_WRITE:
			errstr = "PERMISSION_DENIED_TO_WRITE";
		break;

		case OUT_OF_DISK_SPACE:
			errstr = "OUT_OF_DISK_SPACE";
		break;
		
		case FORMAT_UNACCEPTABLE:
			errstr = "UNACCEPTABLE_IMAGE_FORMAT";
		break;
		
		case UNKNOWN:
			errstr = "UNKNOWN";
		break;
	}
	
	return errstr;
}


// MARK: @interface
class Imlib2 : public ObjectWrap
{
    // MARK: @public
    public:
        Imlib2();
        ~Imlib2();
        static void Initialize( Handle<Object> target );
    // MARK: @private
    private:
        Imlib_Image img;
        int attached;
        const char *format;
        const char *format_to;
        const char *src;
        unsigned int quality;
        double scale;
        int cropped;
        int resized;
        int x;
        int y;
        ImageSize size;
        ImageSize crop;
        ImageSize resize;
        pthread_mutex_t mutex;
        
        // new
        static Handle<Value> New( const Arguments& argv );
        // setter/getter
        static Handle<Value> getFormat( Local<String> prop, const AccessorInfo &info );
        static void setFormat( Local<String> prop, Local<Value> val, const AccessorInfo &info );
        static Handle<Value> getRawWidth( Local<String> prop, const AccessorInfo &info );
        static Handle<Value> getRawHeight( Local<String> prop, const AccessorInfo &info );
        static Handle<Value> getWidth( Local<String> prop, const AccessorInfo &info );
        static Handle<Value> getHeight( Local<String> prop, const AccessorInfo &info );
        static Handle<Value> getQuality( Local<String> prop, const AccessorInfo &info );
        static void setQuality( Local<String> prop, Local<Value> val, const AccessorInfo &info );
        
        static Handle<Value> fnCrop( const Arguments &argv );
        static Handle<Value> fnScale( const Arguments& argv );
        static Handle<Value> fnResize( const Arguments& argv );
        static Handle<Value> fnResizeByWidth( const Arguments& argv );
        static Handle<Value> fnResizeByHeight( const Arguments& argv );
        static Handle<Value> fnSave( const Arguments& argv );
        
        /*/ render
        static int renderBeginEIO( eio_req *req );
        static int renderEndEIO( eio_req *req );
        static Handle<Value> render( const Arguments &argv );
        */
};

// MARK: @implements
Imlib2::Imlib2()
{
    img = NULL;
    attached = 0;
    format = NULL;
    format_to = NULL;
    src = NULL;
    quality = 100;
    scale = 100.0;
    cropped = resized = 0;
    x = y = 0;
    size.w = crop.w = resize.w = 0;
    size.h = crop.h = resize.h = 0;
    size.aspect = crop.aspect = 1;
//    mutex = NULL;
}

Imlib2::~Imlib2()
{
    pthread_mutex_destroy( &mutex );
    if( src ){
        free( (void*)src );
    }
    if( format_to ){
        free( (void*)format_to );
    }
 	if( imlib_context_get_image() )
	{
		if( imlib_get_cache_size() ){
			imlib_free_image_and_decache();
		}
		else {
			imlib_free_image();
		}
	}
}

Handle<Value> Imlib2::New( const Arguments& argv )
{
    HandleScope scope;
    Handle<Value> retval = Null();
    const unsigned int argc = argv.Length();
    
    if( argc && argv[0]->IsString() )
    {
        Imlib2 *ctx = new Imlib2();
        ImageErrorType_e imerr = NOERR;
        
        ctx->img = imlib_load_image_with_error_return( *String::Utf8Value( argv[0] ), (Imlib_Load_Error*)&imerr );
        if( imerr ){
            delete ctx;
            retval = ThrowException( Exception::Error( String::New( ImlibStrError(imerr) ) ) );
        }
        else
        {
            imlib_context_set_image( ctx->img );
            ctx->attached = 1;

            // check image format
            ctx->format = imlib_image_format();
            for( int i = 1; i < argc; i++ )
            {
                if( argv[i]->IsString() )
                {
                    if( strcmp( ctx->format, *String::Utf8Value( argv[i] ) ) ){
                        imerr = FORMAT_UNACCEPTABLE;
                        retval = ThrowException( Exception::Error( String::New( ImlibStrError(imerr) ) ) );
                        break;
                    }
                }
                else {
                    imerr = UNKNOWN;
                    retval = ThrowException( Exception::TypeError( String::New( "new Imlib2( path_to_image:String, accept_format:String, ... )" ) ) );
                    break;
                }
            }
            
            if( retval->IsNull() ){
                ctx->src = strdup( *String::Utf8Value( argv[0] ) );
                ctx->size.w = ctx->crop.w = ctx->resize.w = imlib_image_get_width();
                ctx->size.h = ctx->crop.h = ctx->resize.h = imlib_image_get_height();
                ctx->size.aspect = ctx->crop.aspect = (double)ctx->size.w/(double)ctx->size.h;
                pthread_mutex_init( &ctx->mutex, NULL );
                ctx->Wrap( argv.This() );
                retval = argv.This();
            }
            else {
                delete ctx;
            }
        }
    }
    else {
        retval = ThrowException( Exception::TypeError( String::New( "new Imlib2( path_to_image:String )" ) ) );
    }
    
    return scope.Close( retval );
}

Handle<Value> Imlib2::getFormat( Local<String>, const AccessorInfo &info )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, info.This() );
    
    return scope.Close( String::New( ctx->format ) );
}

void Imlib2::setFormat( Local<String>, Local<Value> val, const AccessorInfo &info )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, info.This() );
    
    if( val->IsString() && val->ToString()->Length() ){
        ctx->format_to = strdup( *String::Utf8Value( val ) );
    }
}

Handle<Value> Imlib2::getRawWidth( Local<String>, const AccessorInfo &info )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, info.This() );
    return scope.Close( Number::New( ctx->size.w ) );
}
Handle<Value> Imlib2::getRawHeight( Local<String>, const AccessorInfo &info )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, info.This() );
    return scope.Close( Number::New( ctx->size.h ) );
}

Handle<Value> Imlib2::getWidth( Local<String>, const AccessorInfo &info )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, info.This() );
    return scope.Close( Number::New( ( ctx->resized ) ? ctx->resize.w : ctx->crop.w ) );
}
Handle<Value> Imlib2::getHeight( Local<String>, const AccessorInfo &info )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, info.This() );
    return scope.Close( Number::New( ( ctx->resized ) ? ctx->resize.h : ctx->crop.h ) );
}

Handle<Value> Imlib2::getQuality( Local<String>, const AccessorInfo &info )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, info.This() );
    return scope.Close( Number::New( ctx->quality ) );
}
void Imlib2::setQuality( Local<String>, Local<Value> val, const AccessorInfo &info )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, info.This() );
    
    if( val->IsNumber() )
    {
        ctx->quality = val->Uint32Value();
        if( ctx->quality > 100 ){
            ctx->quality = 100;
        }
    }
}

Handle<Value> Imlib2::fnCrop( const Arguments &argv )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, argv.This() );
    Handle<Value> retval = Boolean::New( false );
    const int argc = argv.Length();
    double aspect;
    
    if( argc < 1 || !( aspect = argv[0]->NumberValue() ) ){
        retval = ThrowException( Exception::TypeError( String::New( "crop( aspect:Number > 0, align:Number )" ) ) );
    }
    else
    {
		ctx->cropped = 1;
		if( ctx->size.aspect > aspect )
        {
			ctx->crop.w = ctx->size.h * aspect;
			ctx->crop.h = ctx->size.h;
            if( argc > 1 && argv[1]->IsNumber() )
            {
                switch( argv[1]->Uint32Value() )
                {
                    case ALIGN_LEFT:
                        ctx->x = 0;
                    break;
                    
                    case ALIGN_CENTER:
                        ctx->x = ( ctx->size.w - ctx->crop.w ) / 2;
                    break;
                    
                    case ALIGN_RIGHT:
                        ctx->x = ctx->size.w - ctx->crop.w;
                    break;
                    
                    case ALIGN_NONE:
                    break;
                }
            }
		}
		else if( ctx->size.aspect < aspect )
        {
			ctx->crop.h = ctx->size.w / aspect;
			ctx->crop.w = ctx->size.w;
            if( argc > 1 && argv[1]->IsNumber() )
            {
                switch( argv[1]->Uint32Value() )
                {
                    case ALIGN_TOP:
                        ctx->y = 0;
                    break;
                    
                    case ALIGN_MIDDLE:
                        ctx->y = ( ctx->size.h - ctx->crop.h ) / 2;
                    break;
                    
                    case ALIGN_BOTTOM:
                        ctx->y = ctx->size.h - ctx->crop.h;
                    break;
                    
                    case ALIGN_NONE:
                    break;
                }
            }
		}
		else {
			ctx->cropped = 0;
		}
		
		if( ctx->cropped ){
			ctx->crop.aspect = (double)ctx->crop.w/(double)ctx->crop.h;
            retval = Boolean::New( true );
		}
	}
    return scope.Close( retval );
}

Handle<Value> Imlib2::fnScale( const Arguments &argv )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, argv.This() );
    Handle<Value> retval = Undefined();
    const int argc = argv.Length();
    double per;
    
    if( argc < 1 || !argv[0]->IsNumber() || ( per = argv[0]->NumberValue() ) <= 0.0 ){
        retval = ThrowException( Exception::TypeError( String::New( "scale( percentages:Number > 0 )" ) ) );
    }
    else
    {
        double w = ctx->size.w;
        double h = ctx->size.h;
        
        // if cropped
        if( ctx->cropped ){
            w = ctx->crop.w;
            h = ctx->crop.h;
        }
        
        ctx->scale = per;
		ctx->resize.w = ( w / 100 ) * per;
		ctx->resize.h = ( h / 100 ) * per;
		ctx->resized = 1;
    }
    
    return scope.Close( retval );
}

Handle<Value> Imlib2::fnResize( const Arguments &argv )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, argv.This() );
    Handle<Value> retval = Undefined();
    const int argc = argv.Length();
    unsigned int width,height;
    
    if( argc < 2 || 
        !argv[0]->IsNumber() || ( width = argv[0]->Uint32Value() ) < 1 ||
        !argv[1]->IsNumber() || ( height = argv[1]->Uint32Value() ) < 1 ){
        retval = ThrowException( Exception::TypeError( String::New( "resize( width:Number > 0, height:Number > 0 )" ) ) );
    }
    else
    {
        double w = ctx->size.w;
        double h = ctx->size.h;
        
        // if cropped
        if( ctx->cropped ){
            w = ctx->crop.w;
            h = ctx->crop.h;
        }
        
        if( w != width || h != height ){
            ctx->resize.w = width;
            ctx->resize.h = height;
            ctx->resized = 1;
        }
    }
    
    return scope.Close( retval );
}

Handle<Value> Imlib2::fnResizeByWidth( const Arguments &argv )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, argv.This() );
    Handle<Value> retval = Undefined();
    const int argc = argv.Length();
    unsigned int width;
    
    if( argc < 1 || !argv[0]->IsNumber() || ( width = argv[0]->Uint32Value() ) < 1 ){
        retval = ThrowException( Exception::TypeError( String::New( "resizeByWidth( width:Number > 0 )" ) ) );
    }
    else
    {
        double w = ctx->size.w;
        double h = ctx->size.h;
        double aspect = ctx->size.aspect;
	
        // if cropped
        if( ctx->cropped ){
            w = ctx->crop.w;
            h = ctx->crop.h;
            aspect = ctx->crop.aspect;
        }
        
        if( w != width ){
            ctx->resize.w = width;
            ctx->resize.h = width / aspect;
            ctx->resized = 1;
        }
    }
    
    return scope.Close( retval );
}

Handle<Value> Imlib2::fnResizeByHeight( const Arguments &argv )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, argv.This() );
    Handle<Value> retval = Undefined();
    const int argc = argv.Length();
    unsigned int height;
    
    if( argc < 1 || !argv[0]->IsNumber() || ( height = argv[0]->Uint32Value() ) < 1 ){
        retval = ThrowException( Exception::TypeError( String::New( "resizeByHeight( height:Number > 0 )" ) ) );
    }
    else
    {
        double w = ctx->size.w;
        double h = ctx->size.h;
        double aspect = ctx->size.aspect;
	
        // if cropped
        if( ctx->cropped ){
            w = ctx->crop.w;
            h = ctx->crop.h;
            aspect = ctx->crop.aspect;
        }
        
        if( h != height ){
            ctx->resize.w = height * aspect;
            ctx->resize.h = height;
            ctx->resized = 1;
        }
    }
    
    return scope.Close( retval );
}


Handle<Value> Imlib2::fnSave( const Arguments &argv )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, argv.This() );
    Handle<Value> retval = Undefined();
    const int argc = argv.Length();
    
    if( argc < 1 || !argv[0]->IsString() || !argv[0]->ToString()->Length() ){
        retval = ThrowException( Exception::TypeError( String::New( "save( path_to_file:String )" ) ) );
    }
    else
    {
        ImageErrorType_e imerr = NOERR;
        Imlib_Image img, clone;
        const char *path = strdup( *String::Utf8Value( argv[0] ) );
        
        imlib_context_set_image( ctx->img );
        // clone
        clone = imlib_clone_image();
        // crop
        if( ctx->cropped ){
            img = imlib_create_cropped_image( ctx->x, ctx->y, ctx->crop.w, ctx->crop.h );
            imlib_free_image_and_decache();
            ctx->img = img;
            imlib_context_set_image( ctx->img );
        }
        // resize
        if( ctx->resized )
        {
            if( ctx->cropped ){
                img = imlib_create_cropped_scaled_image( 0, 0, ctx->crop.w, ctx->crop.h, ctx->resize.w, ctx->resize.h );
            }
            else{
                img = imlib_create_cropped_scaled_image( 0, 0, ctx->size.w, ctx->size.h, ctx->resize.w, ctx->resize.h );
            }
            imlib_free_image_and_decache();
            ctx->img = img;
            imlib_context_set_image( ctx->img );
        }
        // quality
        imlib_image_attach_data_value( "quality", NULL, ctx->quality, NULL );
        // format
        if( ctx->format_to ){
            imlib_image_set_format( ctx->format_to );
        }

        imlib_save_image_with_error_return( path, (ImlibLoadError*)&imerr );
        imlib_free_image_and_decache();
        free( (void*)path );
        
        ctx->img = clone;
        imlib_context_set_image( ctx->img );
        // failed
        if( imerr ){
            retval = ThrowException( Exception::Error( String::New( ImlibStrError( imerr ) ) ) );
        }
    }
    
    return scope.Close( retval );
}

/*
Handle<Value> Imlib2::save( const Arguments &argv )
{
    HandleScope scope;
    Imlib2 *cs = ObjectUnwrap( Imlib2, argv.This() );
    const int argc = argv.Length();
    Handle<Value> retval = Undefined();
    ParseCtx_t *ctx = NULL;
    bool callback = false;
    
    // invalid arguments
    if( !argv[0]->IsString() || ( 1 < argc && !( callback = argv[1]->IsFunction() ) ) ){
        retval = ThrowException( Exception::TypeError( String::New( "save( path_to_file:String, [callback:Function] )" ) ) );
    }
    // save async
    else if( callback )
    {
        Baton_t *baton = new Baton_t();
        baton->ctx = (void*)ctx;
        baton->data = NULL;
        // detouch from GC
        baton->callback = Persistent<Function>::New( Local<Function>::Cast( argv[1] ) );
        cs->Ref();
        baton->req = eio_custom( saveBeginEIO, EIO_PRI_DEFAULT, saveEndEIO, baton );
        ev_ref(EV_DEFAULT_UC);
    }
    // save sync
    else
    {
        char *estr = NULL;
        
        // save
        if( ( estr = CHECK_NEOERR( cs_render( ctx->csp, &page, callbackRender ) ) ) ){
            retval = ThrowException( Exception::Error( String::New( estr ) ) );
            free(estr);
        }
        else {
            retval = String::New( page.buf );
        }
    }
    
    return scope.Close( retval );
}


int Imlib2::saveBeginEIO( eio_req *req )
{
    Baton_t *baton = static_cast<Baton_t*>( req->data );
    ParseCtx_t *ctx = (ParseCtx_t*)baton->ctx;
    
    if( pthread_mutex_lock( &ctx->mutex ) ){
        baton->nerr = nerr_raise( NERR_SYSTEM, "Mutex lock failed: %s", strerror(errno) );
    }
    else
    {
        STRING page;
        
        string_init(&page);
        if( STATUS_OK == ( baton->nerr = cs_render( ctx->csp, &page, callbackRender ) ) ){
            baton->data = malloc( page.len+1 );
            memcpy( (void*)baton->data, (void*)page.buf, page.len );
            ((char*)baton->data)[page.len] = 0;
        }
        string_clear(&page);
        errno = 0;
        if( pthread_mutex_unlock( &ctx->mutex ) )
        {
            free( baton->data );
            if( STATUS_OK != baton->nerr ){
                free(baton->nerr);
            }
            baton->nerr = nerr_raise( NERR_LOCK, "Mutex unlock failed: %s", strerror(errno) );
        }
    }
    
    return 0;
}

int Imlib2::saveEndEIO( eio_req *req )
{
    HandleScope scope;
    Baton_t *baton = static_cast<Baton_t*>(req->data);
    ParseCtx_t *ctx = (ParseCtx_t*)baton->ctx;
    Handle<Primitive> t = Undefined();
    Local<Value> argv[] = {
        reinterpret_cast<Local<Value>&>(t),
        reinterpret_cast<Local<Value>&>(t)
    };
    
    ev_unref(EV_DEFAULT_UC);
    ctx->cs->Unref();
    
    if( STATUS_OK == baton->nerr ){
        argv[1] = String::New( (char*)baton->data );
        free( baton->data );
    }
    else {
        const char *errstr = CHECK_NEOERR( baton->nerr );
        baton->nerr = STATUS_OK;
        argv[0] = Exception::Error( String::New( errstr ) );
        free( (void*)errstr );
    }
    
    TryCatch try_catch;
    // call js function by callback function context
    baton->callback->Call( baton->callback, 2, argv );
    if( try_catch.HasCaught() ){
        FatalException(try_catch);
    }
    // remove callback
    baton->callback.Dispose();
    delete baton;
    
    eio_cancel(req);
    
    return 0;
}
*/

void Imlib2::Initialize( Handle<Object> target )
{
    HandleScope scope;
    Local<FunctionTemplate> t = FunctionTemplate::New( New );
    
    t->InstanceTemplate()->SetInternalFieldCount(1);
    t->SetClassName( String::NewSymbol("Imlib2") );
    NODE_SET_PROTOTYPE_METHOD( t, "crop", fnCrop );
    NODE_SET_PROTOTYPE_METHOD( t, "scale", fnScale );
    NODE_SET_PROTOTYPE_METHOD( t, "resize", fnResize );
    NODE_SET_PROTOTYPE_METHOD( t, "resizeByWidth", fnResizeByWidth );
    NODE_SET_PROTOTYPE_METHOD( t, "resizeByHeight", fnResizeByHeight );
    NODE_SET_PROTOTYPE_METHOD( t, "save", fnSave );
    
    Local<ObjectTemplate> proto = t->PrototypeTemplate();
    proto->SetAccessor(String::NewSymbol("format"), getFormat, setFormat );
    proto->SetAccessor(String::NewSymbol("quality"), getQuality, setQuality );
    proto->SetAccessor(String::NewSymbol("rawWidth"), getRawWidth );
    proto->SetAccessor(String::NewSymbol("rawHeight"), getRawHeight );
    proto->SetAccessor(String::NewSymbol("width"), getWidth );
    proto->SetAccessor(String::NewSymbol("height"), getHeight );
    
    target->Set( String::NewSymbol("Imlib2"), t->GetFunction() );
}

extern "C" {
    static void init( Handle<Object> target ){
        HandleScope scope;
        Imlib2::Initialize( target );
    }
    NODE_MODULE( Imlib2, init );
};
