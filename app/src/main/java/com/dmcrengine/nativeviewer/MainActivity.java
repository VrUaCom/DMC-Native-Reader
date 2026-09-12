package com.dmcrengine.nativeviewer;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.OutputStream;
import java.util.ArrayDeque;

public final class MainActivity extends Activity {
    private static final int REQUEST_OPEN = 1001;
    private static final int REQUEST_ATTACH_PTX = 1002;
    private static final int REQUEST_EXPORT_SINGLE = 1003;
    private static final int REQUEST_EXPORT_GALLERY = 1004;
    private static final int TOOL_SIZE_DP = 48;
    private static final int TOOL_GAP_DP = 4;
    private static final int UV_EXPORT_SIZE = 1024;

    private static final class NavigationEntry {
        final long session;
        final String title;
        NavigationEntry(long session, String title) { this.session = session; this.title = title; }
    }

    private DmcRenderView renderView;
    private ChildResourceBrowserView childBrowser;
    private TextView titleView;
    private Button ptxButton, resetButton, wireButton, hierarchyButton, uvButton, infoButton;
    private long session;
    private long pendingExportSession;
    private final ArrayDeque<NavigationEntry> navigation = new ArrayDeque<>();
    private BlackWidowState state = BlackWidowState.empty();
    private String infoText = "";

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState); buildUi(); handleIncomingIntent(getIntent());
    }
    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent); setIntent(intent); handleIncomingIntent(intent);
    }
    private int dp(int v){ return Math.round(v * getResources().getDisplayMetrics().density); }
    private Button button(String text,String desc,float size){
        Button b=new Button(this);b.setText(text);b.setTextSize(size);b.setAllCaps(false);b.setMinWidth(0);b.setMinimumWidth(0);b.setMinHeight(0);b.setMinimumHeight(0);b.setPadding(0,0,0,0);b.setGravity(Gravity.CENTER);b.setContentDescription(desc);return b;
    }
    private void addTool(LinearLayout bar,Button b){LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(dp(TOOL_SIZE_DP),dp(TOOL_SIZE_DP));p.setMarginStart(dp(TOOL_GAP_DP));p.setMarginEnd(dp(TOOL_GAP_DP));bar.addView(b,p);}
    private void available(Button b,boolean yes){b.setEnabled(yes);b.setAlpha(yes?1f:.35f);}
    private void toggle(Button b,boolean yes,boolean active){b.setEnabled(yes);b.setActivated(yes&&active);b.setAlpha(!yes?.35f:(active?1f:.78f));}
    private void refreshState(){state=session==0?BlackWidowState.empty():BlackWidowState.fromNative(NativeBridge.blackWidowState(session));}
    private boolean canAttachPtx(){return session!=0&&state.canAttachTextureCompanion;}

    private boolean canExportPng(){
        return session!=0&&state.canExportPng;
    }

    private void applyPresentation(){
        boolean gallery=session!=0&&state.childBrowserMode;
        if(gallery){renderView.setVisibility(View.GONE);childBrowser.setVisibility(View.VISIBLE);childBrowser.setSession(session);}else{childBrowser.setSession(0);childBrowser.setVisibility(View.GONE);renderView.setVisibility(View.VISIBLE);}
    }
    private void applyUi(){
        applyPresentation();
        boolean export=canExportPng();
        if(export){resetButton.setText("↓");resetButton.setContentDescription(NativeBridge.childResourceCount(session)>0?"Export all images as PNG":"Export image as PNG");available(resetButton,true);}else{resetButton.setText("🔄");resetButton.setContentDescription("Reset view");available(resetButton,session!=0&&state.canRender);}
        boolean ptx=canAttachPtx();ptxButton.setVisibility(ptx?View.VISIBLE:View.GONE);toggle(ptxButton,ptx,state.textureCompanionAttached);
        toggle(wireButton,session!=0&&(state.canWireframe||state.canInspectMeshes),renderView.isWireframe());
        boolean h=session!=0&&state.canShowHierarchy&&!renderView.isUvLayoutVisible();renderView.setHierarchyAvailable(h);toggle(hierarchyButton,h||state.canInspectHierarchy,renderView.isHierarchyVisible());
        toggle(uvButton,session!=0&&(state.canShowUv||state.canInspectUv),renderView.isUvLayoutVisible());
        available(infoButton,session!=0?state.canInspect:!infoText.isEmpty());
    }
    private void applyInsets(LinearLayout root){root.setOnApplyWindowInsetsListener((v,i)->{int l,t,r,b;if(Build.VERSION.SDK_INT>=30){Insets x=i.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());l=x.left;t=x.top;r=x.right;b=x.bottom;}else{l=i.getSystemWindowInsetLeft();t=i.getSystemWindowInsetTop();r=i.getSystemWindowInsetRight();b=i.getSystemWindowInsetBottom();}v.setPadding(l,t,r,b);return i;});root.requestApplyInsets();}

    private void buildUi(){
        LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setBackgroundColor(0xff0b0b0e);applyInsets(root);
        LinearLayout header=new LinearLayout(this);header.setOrientation(LinearLayout.HORIZONTAL);header.setGravity(Gravity.CENTER_VERTICAL);
        Button back=button("←","Back",28f);back.setOnClickListener(v->navigateBack());header.addView(back,new LinearLayout.LayoutParams(dp(TOOL_SIZE_DP),dp(TOOL_SIZE_DP)));
        titleView=new TextView(this);titleView.setTextColor(Color.WHITE);titleView.setTextSize(15f);titleView.setSingleLine(true);titleView.setEllipsize(TextUtils.TruncateAt.END);titleView.setPadding(dp(12),dp(8),dp(10),dp(6));titleView.setText("DMC Native Reader");header.addView(titleView,new LinearLayout.LayoutParams(0,LinearLayout.LayoutParams.WRAP_CONTENT,1f));
        ptxButton=button(".PTX","Attach PTX texture companion",12f);ptxButton.setVisibility(View.GONE);ptxButton.setOnClickListener(v->choosePtx());header.addView(ptxButton,new LinearLayout.LayoutParams(dp(TOOL_SIZE_DP),dp(TOOL_SIZE_DP)));
        root.addView(header,new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT));
        FrameLayout viewport=new FrameLayout(this);renderView=new DmcRenderView(this);viewport.addView(renderView,new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,FrameLayout.LayoutParams.MATCH_PARENT));childBrowser=new ChildResourceBrowserView(this);childBrowser.setVisibility(View.GONE);childBrowser.setListener(this::openChild);viewport.addView(childBrowser,new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,FrameLayout.LayoutParams.MATCH_PARENT));root.addView(viewport,new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,0,1f));
        LinearLayout bar=new LinearLayout(this);bar.setOrientation(LinearLayout.HORIZONTAL);bar.setGravity(Gravity.CENTER);bar.setPadding(dp(8),dp(6),dp(8),dp(8));
        Button open=button("↑","Open MOD / SCM / DDS / PTX",28f);open.setOnClickListener(v->chooseFile());addTool(bar,open);
        resetButton=button("🔄","Reset view",20f);resetButton.setOnClickListener(v->handleResetOrExport());addTool(bar,resetButton);
        wireButton=button("W","Wireframe",18f);wireButton.setOnClickListener(v->{if(state.canWireframe){renderView.toggleWireframe();applyUi();}});addTool(bar,wireButton);
        hierarchyButton=button("🦴","Bones / hierarchy",20f);hierarchyButton.setOnClickListener(v->{if(state.canShowHierarchy){renderView.toggleHierarchy();applyUi();}});addTool(bar,hierarchyButton);
        uvButton=button("UV","UV layout",14f);uvButton.setOnClickListener(v->{if(!state.canShowUv)return;long g=NativeBridge.openUvGallery(session);if(g==0){Toast.makeText(this,"UV maps unavailable: incomplete bindings",Toast.LENGTH_LONG).show();return;}navigateTo(g,titleView.getText()+" · UV");});addTool(bar,uvButton);
        bindHold(uvButton,NativeBridge.INSPECT_UV);bindHold(wireButton,NativeBridge.INSPECT_MESHES);bindHold(hierarchyButton,NativeBridge.INSPECT_HIERARCHY);
        infoButton=button("ℹ","Resource information",22f);infoButton.setOnClickListener(v->showInfo(infoText));addTool(bar,infoButton);
        root.addView(bar,new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,LinearLayout.LayoutParams.WRAP_CONTENT));setContentView(root);showIdle();
    }
    private void bindHold(Button b,int topic){b.setOnLongClickListener(v->{if(session==0)return false;showInfo(NativeBridge.inspectionTopic(session,topic));return true;});}
    private void showInfo(String text){TextView d=new TextView(this);d.setText(text==null||text.isEmpty()?"No resource information available.":text);d.setTextColor(Color.WHITE);d.setTextSize(13f);d.setTypeface(Typeface.MONOSPACE);d.setTextIsSelectable(true);d.setPadding(dp(16),dp(12),dp(16),dp(20));ScrollView s=new ScrollView(this);s.setBackgroundColor(0xff141418);s.addView(d);new AlertDialog.Builder(this).setTitle(titleView.getText()).setView(s).setPositiveButton("Close",null).show();}

    private void chooseFile(){Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT);i.addCategory(Intent.CATEGORY_OPENABLE);i.setType("*/*");i.putExtra(Intent.EXTRA_MIME_TYPES,new String[]{"application/vnd.dmc.scm","application/vnd.dmc.mod","application/vnd.dmc.ptx","image/vnd-ms.dds","application/octet-stream","*/*"});startActivityForResult(i,REQUEST_OPEN);}
    private void choosePtx(){if(!canAttachPtx())return;Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT);i.addCategory(Intent.CATEGORY_OPENABLE);i.setType("*/*");i.putExtra(Intent.EXTRA_MIME_TYPES,new String[]{"application/vnd.dmc.ptx","application/octet-stream","*/*"});startActivityForResult(i,REQUEST_ATTACH_PTX);}
    private void handleResetOrExport(){if(session==0)return;if(canExportPng())chooseExport();else if(state.canRender)renderView.resetView();}
    private void chooseExport(){pendingExportSession=session;if(NativeBridge.childResourceCount(session)>0){Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_WRITE_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION|Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);startActivityForResult(i,REQUEST_EXPORT_GALLERY);}else{Intent i=new Intent(Intent.ACTION_CREATE_DOCUMENT);i.addCategory(Intent.CATEGORY_OPENABLE);i.setType("image/png");i.putExtra(Intent.EXTRA_TITLE,singleName());startActivityForResult(i,REQUEST_EXPORT_SINGLE);}}

    @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(result!=RESULT_OK||data==null){if(request==REQUEST_EXPORT_SINGLE||request==REQUEST_EXPORT_GALLERY)pendingExportSession=0;return;}
        if(request==REQUEST_EXPORT_SINGLE){Uri u=data.getData();if(u!=null)exportCurrent(u);pendingExportSession=0;return;}
        if(request==REQUEST_EXPORT_GALLERY){Uri u=data.getData();if(u!=null){persist(u,data.getFlags(),true);exportGallery(u);}pendingExportSession=0;return;}
        Uri u=data.getData();if(u==null)return;persist(u,data.getFlags(),false);if(request==REQUEST_ATTACH_PTX)attachPtx(u);else if(request==REQUEST_OPEN)openUri(u);
    }
    private void persist(Uri u,int flags,boolean write){int wanted=Intent.FLAG_GRANT_READ_URI_PERMISSION|(write?Intent.FLAG_GRANT_WRITE_URI_PERMISSION:0);int f=flags&wanted;if(f==0)return;try{getContentResolver().takePersistableUriPermission(u,f);}catch(SecurityException ignored){}}

    private void handleIncomingIntent(Intent i){if(i==null){showIdle();return;}Uri u=i.getData();if(u==null&&Intent.ACTION_SEND.equals(i.getAction())){try{Object x=i.getParcelableExtra(Intent.EXTRA_STREAM);if(x instanceof Uri)u=(Uri)x;}catch(RuntimeException ignored){}}if(u==null){ClipData c=i.getClipData();if(c!=null&&c.getItemCount()>0)u=c.getItemAt(0).getUri();}if(u==null)showIdle();else openUri(u);}
    private String displayName(Uri u){if("content".equals(u.getScheme()))try(Cursor c=getContentResolver().query(u,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){if(c!=null&&c.moveToFirst()){int x=c.getColumnIndex(OpenableColumns.DISPLAY_NAME);if(x>=0)return c.getString(x);}}catch(RuntimeException ignored){}String p=u.getPath();return p==null?"resource.bin":new File(p).getName();}
    private ParcelFileDescriptor openFd(Uri u)throws FileNotFoundException{if("file".equals(u.getScheme())&&u.getPath()!=null)return ParcelFileDescriptor.open(new File(u.getPath()),ParcelFileDescriptor.MODE_READ_ONLY);return getContentResolver().openFileDescriptor(u,"r");}

    private void openUri(Uri u){closeAll();String name=displayName(u);long h=0;try(ParcelFileDescriptor p=openFd(u)){if(p!=null)h=NativeBridge.open(p.getFd(),name);}catch(Exception e){Toast.makeText(this,"Could not read file",Toast.LENGTH_LONG).show();}
        if(h==0){titleView.setText(name);infoText=name+"\nRejected: supported route failed structural validation or format is outside MOD / SCM / DDS / PTX.";applyUi();Toast.makeText(this,"Unsupported or malformed DMC resource",Toast.LENGTH_LONG).show();return;}
        activate(h,name);
    }

    private void attachPtx(Uri u){if(!canAttachPtx())return;String n=displayName(u);boolean ok=false;try(ParcelFileDescriptor p=openFd(u)){if(p!=null)ok=NativeBridge.attachPtx(session,p.getFd(),n);}catch(Exception ignored){}refreshState();String d=NativeBridge.textureAttachmentInfo(session);if(ok){renderView.renderNow();rebuildInfo(titleView.getText().toString());applyUi();}Toast.makeText(this,d==null||d.isEmpty()?(ok?"PTX textures attached":"PTX could not be matched"):d,Toast.LENGTH_LONG).show();}
    private void activate(long h,String title){session=h;titleView.setText(title);renderView.setSession(h);refreshState();rebuildInfo(title);applyUi();}
    private void rebuildInfo(String name){String inspect=session==0?"":NativeBridge.inspection(session);String nativeInfo=session==0?"":NativeBridge.info(session);infoText=name+"\n\nSTRUCTURE\n"+(inspect==null||inspect.isEmpty()?"No typed inspection document.\n":inspect)+"\nSESSION / EVIDENCE\n"+nativeInfo;}
    private void showIdle(){titleView.setText("DMC Native Reader");state=BlackWidowState.empty();infoText="DMC Native Reader "+BuildConfig.VERSION_NAME+"\nArchitecture v2 core: MOD / SCM / DDS / PTX.\nPNG export is native-capability controlled for PTX/DDS and UV galleries/maps.";applyUi();}

    private Bitmap bitmapFor(long h){if(h==0)return null;BlackWidowState s=BlackWidowState.fromNative(NativeBridge.blackWidowState(h));int w,hg;int[] px;if(s.uvMapView){w=UV_EXPORT_SIZE;hg=UV_EXPORT_SIZE;px=NativeBridge.render(h,w,hg,0f,0f,1f,0);}else{w=NativeBridge.imagePreviewWidth(h);hg=NativeBridge.imagePreviewHeight(h);if(w<=0||hg<=0)return null;px=NativeBridge.imagePreview(h);}long n=(long)w*hg;if(px==null||n<=0||n>Integer.MAX_VALUE||px.length!=(int)n)return null;try{return Bitmap.createBitmap(px,w,hg,Bitmap.Config.ARGB_8888);}catch(Throwable e){return null;}}
    private boolean save(Bitmap b,Uri u){if(b==null||u==null)return false;try(OutputStream o=getContentResolver().openOutputStream(u,"w")){return o!=null&&b.compress(Bitmap.CompressFormat.PNG,100,o);}catch(Exception e){return false;}}
    private void exportCurrent(Uri u){long h=pendingExportSession!=0?pendingExportSession:session;Bitmap b=bitmapFor(h);boolean ok=save(b,u);if(b!=null)b.recycle();Toast.makeText(this,ok?"PNG saved":"Could not export PNG",Toast.LENGTH_LONG).show();}
    private void exportGallery(Uri tree){long h=pendingExportSession!=0?pendingExportSession:session;int count=NativeBridge.childResourceCount(h);if(count<=0){Toast.makeText(this,"No gallery images to export",Toast.LENGTH_LONG).show();return;}Uri parent;try{String id=DocumentsContract.getTreeDocumentId(tree);parent=DocumentsContract.buildDocumentUriUsingTree(tree,id);}catch(RuntimeException e){Toast.makeText(this,"Selected folder is not writable",Toast.LENGTH_LONG).show();return;}int saved=0;for(int i=0;i<count;i++){long child=0;Bitmap b=null;try{String title=NativeBridge.childResourceTitle(h,i);child=NativeBridge.openChild(h,i);if(child==0)continue;b=bitmapFor(child);if(b==null)continue;Uri target=DocumentsContract.createDocument(getContentResolver(),parent,"image/png",childName(title));if(target!=null&&save(b,target))saved++;}catch(Exception ignored){}finally{if(b!=null)b.recycle();if(child!=0)NativeBridge.close(child);}}Toast.makeText(this,"PNG export: "+saved+"/"+count,Toast.LENGTH_LONG).show();}
    private String sanitize(String s){if(s==null||s.isEmpty())return"image";StringBuilder o=new StringBuilder();for(int i=0;i<s.length();i++){char c=s.charAt(i);if(Character.isLetterOrDigit(c)||c=='-'||c=='_')o.append(c);else if(o.length()>0&&o.charAt(o.length()-1)!='_')o.append('_');}while(o.length()>0&&o.charAt(o.length()-1)=='_')o.deleteCharAt(o.length()-1);return o.length()==0?"image":o.toString();}
    private String stem(String s){int p=s==null?-1:s.lastIndexOf('.');return p>0?s.substring(0,p):(s==null?"resource":s);}
    private String rootTitle(){NavigationEntry r=navigation.peekLast();return r==null?titleView.getText().toString():r.title;}
    private String singleName(){String root=sanitize(stem(rootTitle()));String current=titleView.getText().toString();return current.equals(rootTitle())?root+".png":root+"__"+sanitize(current)+".png";}
    private String childName(String title){return sanitize(stem(rootTitle()))+"__"+sanitize(title)+".png";}

    private void openChild(int index,String title){if(session==0)return;long h=NativeBridge.openChild(session,index);if(h==0){Toast.makeText(this,"Could not open child resource",Toast.LENGTH_LONG).show();return;}navigateTo(h,title);}
    private void navigateTo(long h,String title){navigation.push(new NavigationEntry(session,titleView.getText().toString()));activate(h,title);}
    private boolean parent(){if(navigation.isEmpty())return false;long child=session;renderView.setSession(0);childBrowser.setSession(0);if(child!=0)NativeBridge.close(child);NavigationEntry p=navigation.pop();activate(p.session,p.title);return true;}
    private void navigateBack(){if(!parent())finish();}
    @Override public void onBackPressed(){navigateBack();}
    private void closeAll(){renderView.setSession(0);childBrowser.setSession(0);state=BlackWidowState.empty();pendingExportSession=0;if(session!=0){NativeBridge.close(session);session=0;}while(!navigation.isEmpty()){NavigationEntry e=navigation.pop();if(e.session!=0)NativeBridge.close(e.session);}if(resetButton!=null)applyUi();}
    @Override protected void onDestroy(){closeAll();super.onDestroy();}
}
