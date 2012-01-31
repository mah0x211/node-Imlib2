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


typedef enum ASYNC_TASK_BIT {
    ASYNC_TASK_LOAD = 1 << 0,
    ASYNC_TASK_SAVE = 1 << 1
};
typedef struct {
    void *ctx;
    int task;
    const char *errstr;
    void *udata;
    // callback js function when async is true
    Persistent<Function> callback;
    Handle<Value> retval;
    eio_req *req;
} Baton_t;


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
        Handle<Value> createContext( void );
        Handle<Value> saveContext( const char *path );

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
        
        // thread task
        static int beginEIO( eio_req *req );
        static int endEIO( eio_req *req );
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
    pthread_mutex_init( &mutex, NULL );
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


int Imlib2::beginEIO( eio_req *req )
{
    Baton_t *baton = static_cast<Baton_t*>( req->data );
    Imlib2 *ctx = (Imlib2*)baton->ctx;
    
    // failed to lock mutex
    if( pthread_mutex_lock( &ctx->mutex ) ){
        baton->errstr = strerror(errno);
    }
    else
    {
        if( baton->task & ASYNC_TASK_LOAD ){
            baton->retval = ctx->createContext();
        }
        else if( baton->task & ASYNC_TASK_SAVE ){
            baton->retval = ctx->saveContext( (const char*)baton->udata );
        }
        
        // failed to unlock mutex
        if( pthread_mutex_unlock( &ctx->mutex ) ){
            baton->errstr = strerror(errno);
        }
    }
    
    return 0;
}

int Imlib2::endEIO( eio_req *req )
{
    HandleScope scope;
    Baton_t *baton = static_cast<Baton_t*>(req->data);
    Imlib2 *ctx = (Imlib2*)baton->ctx;
    int task = baton->task;
    Local<Function> cb = Local<Function>::New( baton->callback );
    Handle<Primitive> t = Undefined();
    Local<Value> errstr = reinterpret_cast<Local<Value>&>(t);
    
    ev_unref(EV_DEFAULT_UC);
    ctx->Unref();
    
    if( baton->errstr ){
        errstr = Exception::Error( String::New( baton->errstr ) );
    }
    else if( IsDefined( baton->retval ) ){
        errstr = Exception::Error( baton->retval->ToString() );
    }
    
    // cleanup
    baton->callback.Dispose();
    if( baton->udata ){
        free((void*)baton->udata);
    }
    delete baton;
    
    if( task & ASYNC_TASK_LOAD )
    {
        Local<Value> argv[] = {
            reinterpret_cast<Local<Value>&>(errstr),
            reinterpret_cast<Local<Value>&>(t)
        };
        
        if( IsDefined( errstr ) ){
            delete ctx;
        }
        // context
        else {
            argv[1] = reinterpret_cast<Local<Value>&>(ctx->handle_);
        }
        
        TryCatch try_catch;
        // call js function by callback function context
        // !!!: which is better callback or Context::GetCurrent()->Global() context
        cb->Call( cb, 2, argv );
        if( try_catch.HasCaught() ){
            FatalException(try_catch);
        }
    }
    else if( task & ASYNC_TASK_SAVE )
    {
        Local<Value> argv[] = {
            reinterpret_cast<Local<Value>&>(errstr)
        };
        
        TryCatch try_catch;
        // call js function by callback function context
        cb->Call( ctx->handle_, 1, argv );
        if( try_catch.HasCaught() ){
            FatalException(try_catch);
        }
    }
    
    eio_cancel(req);
    
    return 0;
}

Handle<Value> Imlib2::New( const Arguments& argv )
{
    HandleScope scope;
    Handle<Value> retval = Undefined();
    const unsigned int argc = argv.Length();
    bool callback = false;
    
    if( argc < 1 || 
        !argv[0]->IsString() || !argv[0]->ToString()->Length() ||
        ( argc > 1 && !( callback = argv[1]->IsFunction() ) ) ){
        retval = ThrowException( Exception::TypeError( String::New( "new Imlib2( path_to_image:String, [callback:Function] )" ) ) );
    }
    else
    {
        Imlib2 *ctx = new Imlib2();
        
        ctx->src = strdup( *String::Utf8Value( argv[0] ) );
        
        if( callback )
        {
            Baton_t *baton = new Baton_t();
            
            baton->task = ASYNC_TASK_LOAD;
            baton->ctx = (void*)ctx;
            baton->retval = Undefined();
            baton->errstr = NULL;
            baton->udata = NULL;
            // detouch from GC
            baton->callback = Persistent<Function>::New( Local<Function>::Cast( argv[1] ) );
            ctx->Wrap( argv.This() );
            ctx->Ref();
            baton->req = eio_custom( beginEIO, EIO_PRI_DEFAULT, endEIO, baton );
            ev_ref(EV_DEFAULT_UC);
        }
        else
        {
            retval = ctx->createContext();
            // failed
            if( IsDefined( retval ) ){
                retval = ThrowException( Exception::Error( retval->ToString() ) );
                delete ctx;
            }
            else {
                ctx->Wrap( argv.This() );
                retval = argv.This();
            }
        }
    }
    
    return scope.Close( retval );
}

Handle<Value> Imlib2::createContext( void )
{
    Handle<Value> retval = Undefined();
    ImageErrorType_e imerr = NOERR;
        
    img = imlib_load_image_with_error_return( src, (Imlib_Load_Error*)&imerr );
    if( imerr ){
        retval = String::New( ImlibStrError(imerr) );
    }
    else {
        imlib_context_set_image( img );
        attached = 1;
        format = imlib_image_format();
        size.w = crop.w = resize.w = imlib_image_get_width();
        size.h = crop.h = resize.h = imlib_image_get_height();
        size.aspect = crop.aspect = (double)size.w/(double)size.h;
    }
    
    return retval;
}

Handle<Value> Imlib2::saveContext( const char *path )
{
    Handle<Value> retval = Undefined();
    ImageErrorType_e imerr = NOERR;
    Imlib_Image work = img, clone;
    
    // set current image
    imlib_context_set_image( img );
    // clone current image for backup
    clone = imlib_clone_image();
    
    // crop
    if( cropped ){
        work = imlib_create_cropped_image( x, y, crop.w, crop.h );
        imlib_free_image_and_decache();
        imlib_context_set_image( work );
    }
    // resize
    if( resized )
    {
        if( cropped ){
            work = imlib_create_cropped_scaled_image( 0, 0, crop.w, crop.h, resize.w, resize.h );
        }
        else{
            work = imlib_create_cropped_scaled_image( 0, 0, size.w, size.h, resize.w, resize.h );
        }
        imlib_free_image_and_decache();
        imlib_context_set_image( work );
    }
    // quality
    imlib_image_attach_data_value( "quality", NULL, quality, NULL );
    // format
    if( format_to ){
        imlib_image_set_format( format_to );
    }
    
    imlib_save_image_with_error_return( path, (ImlibLoadError*)&imerr );
    imlib_free_image_and_decache();
    img = clone;
    imlib_context_set_image( img );
    // failed
    if( imerr ){
        retval = String::New( ImlibStrError( imerr ) );
    }
    
    return retval;
}

Handle<Value> Imlib2::fnSave( const Arguments &argv )
{
    HandleScope scope;
    Imlib2 *ctx = ObjectUnwrap( Imlib2, argv.This() );
    Handle<Value> retval = Undefined();
    const int argc = argv.Length();
    bool callback = false;
    
    if( argc < 1 || 
        !argv[0]->IsString() || !argv[0]->ToString()->Length() ||
        ( argc > 1 && !( callback = argv[1]->IsFunction() ) ) ){
        retval = ThrowException( Exception::TypeError( String::New( "save( path_to_file:String, [callback:Function] )" ) ) );
    }
    else
    {
        if( callback )
        {
            Baton_t *baton = new Baton_t();
            
            baton->task = ASYNC_TASK_SAVE;
            baton->ctx = (void*)ctx;
            baton->retval = Undefined();
            baton->errstr = NULL;
            baton->udata = strdup( *String::Utf8Value( argv[0] ) );
            // detouch from GC
            baton->callback = Persistent<Function>::New( Local<Function>::Cast( argv[1] ) );
            ctx->Ref();
            baton->req = eio_custom( beginEIO, EIO_PRI_DEFAULT, endEIO, baton );
            ev_ref(EV_DEFAULT_UC);
        }
        else
        {
            const char *path = strdup( *String::Utf8Value( argv[0] ) );
            ctx->saveContext( path );
            free((void*)path);
        }
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


extern "C" 
{
    static void init( Handle<Object> target )
    {
        HandleScope scope;
        Imlib2::Initialize( target );
    }
    NODE_MODULE( Imlib2, init );
};
